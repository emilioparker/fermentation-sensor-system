use axum::{
    extract::{Query, State}, routing::{get, post}, Json, Router
};
use chrono::{Date, DateTime, FixedOffset, Utc};
use serde::{Deserialize, Serialize};
use tokio::{net::TcpListener, sync::Mutex};
use tower_http::services::ServeDir;
use std::{collections::{HashMap, HashSet}, fs::{self, File, OpenOptions}, net::SocketAddr, path::{self, Path}, sync::{Arc, atomic::AtomicU32}};
use std::io::Write;



#[derive(Deserialize)]
struct GetParams 
{
    sensor: String,
    date: String,
}

#[derive(Deserialize)]
struct GetReportParams 
{
    sensor: String,
}

#[derive(Deserialize)]
struct GetHealthParams
{
    sensor: String,
}

#[derive(Clone, Serialize)]
struct DeviceHealth
{
    version: String,
    sensor_id: String,
    temperature: f32,
    humidity: f32,
    // Seconds since the monitor booted, and the power-on time derived from it
    uptime_s: u64,
    powered_on_at: String,
    received_at: String,
}

#[derive(Clone)]
struct AppState 
{
    last_sample: Arc<tokio::sync::Mutex<HashMap<String, String>>>,
    // Latest health per sensor, in memory only (lost on restart)
    last_health: Arc<tokio::sync::Mutex<HashMap<String, DeviceHealth>>>,
}

fn extract_data(line: &str) -> Option<(String, String, DateTime<FixedOffset>, f32, f32, f32, f32, f32, f32)>
{
    let mut parts = line.split(',');

    let first = parts.next().unwrap_or("").trim();
    println!("data version: {}", if first == "01" || first == "02" || first == "04" || first == "05" { first } else { "legacy" });

    let sensor_id: String;
    let date_str: &str;
    let t0: f32;
    let t1: f32;
    let t2: f32;
    let t3: f32;
    let humidity: f32;
    let dht_temp: f32;

    match first
    {
        // version 02: SENSOR_ID,datetime,t0,t1,t2,humidity,dht_temp (3 sensors)
        "02" =>
        {
            sensor_id = parts.next().unwrap_or("ufo").trim().to_string();
            date_str = parts.next().unwrap_or("");
            t0       = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
            t1       = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
            t2       = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
            t3       = 0.0;
            humidity = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
            dht_temp = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
        },
        // version 01: SENSOR_ID,datetime,t0,t1,t2 (no humidity/dht)
        // version 04: SENSOR_ID,datetime,t0,t1,t2 (SHT41 removed, same layout as 01)
        // version 05: SENSOR_ID,datetime,t0,t1,t2 (same layout as 01)
        "01" | "04" | "05" =>
        {
            sensor_id = parts.next().unwrap_or("ufo").trim().to_string();
            date_str = parts.next().unwrap_or("");
            t0       = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
            t1       = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
            t2       = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
            t3       = 0.0;
            humidity = 0.0;
            dht_temp = 0.0;
        },
        // legacy: SENSOR_ID,datetime,t0,t1,t2 (no version prefix)
        _ =>
        {
            sensor_id = first.to_string();
            date_str  = parts.next().unwrap_or("");
            t0        = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
            t1        = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
            t2        = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
            t3        = 0.0;
            humidity  = 0.0;
            dht_temp  = 0.0;
        },
    }

    match DateTime::parse_from_rfc3339(date_str)
    {
        Ok(parsed_date) =>
        {
            let date_only = parsed_date.format("%Y-%m-%d").to_string();
            Some((date_only, sensor_id, parsed_date, t0, t1, t2, t3, humidity, dht_temp))
        },
        Err(error) =>
        {
            println!("{:?}", error);
            None
        },
    }
}


async fn get_last_date_recorded_async(path_string: &str) -> Option<(u32, DateTime<FixedOffset>)> 
{
    match tokio::fs::read_to_string(path_string).await 
    {
        Ok(content) => 
        {
            let mut count = 0;
            let mut last_line = None;

            for line in content.lines() {
                count += 1;
                last_line = Some(line);
            }
            // let count = content.lines()
            if let Some(last_line) = last_line
            {
                println!("Last line: {}", last_line);
                if let Some((_date_only, _sensor_id, date, ..)) = extract_data(last_line)
                {
                    Some((count,date))
                }
                else 
                {
                    println!("Error extracting data in get last record");
                    None
                }
            } 
            else 
            {
                println!("File is empty");
                None
            }
        }
        Err(err) => 
        {
            if err.kind() == std::io::ErrorKind::NotFound 
            {
                println!("File not found");
            } 
            else 
            {
                eprintln!("Error reading file: {}", err);
            }
            None
        }
    }
}

fn get_data(path_string : &str) -> String
{
    let contents = fs::read_to_string(path_string);
    if let Ok(content) = contents
    {
        return content;
    }
    else 
    {
        println!("file not found {}", path_string);
        return "".to_owned();
    }
}

// voy a recibir data en un rango x
async fn handle_post(State(state): State<AppState>, payload: String) -> String 
{
    println!("HandlePost:Received payload:\n{}", payload);

    // get the correct file

    let mut current_file = "_".to_owned();
    let mut current_date= None;
    let mut active_file: Option<File> =  None;

    println!("HandlePost:Received {} lines", payload.lines().count());
    let lines = payload.lines();
    for line in lines
    {
        if let Some((date_only, sensor_id, date, ..)) = extract_data(line)
        {
            let path_string = format!("data/{}_{}.csv", sensor_id, date_only);

            if current_file != date_only
            {
                current_file = date_only;

                let last_record = get_last_date_recorded_async(&path_string).await;
                println!("---getting last recorded entry {:?}", last_record);
                current_date = last_record.map(|f| f.1);
                
                let new_file = OpenOptions::new()
                    .create(true)   // create file if it doesn't exist
                    .append(true)   // open in append mode
                    .open(&path_string);

                match new_file 
                {
                    Ok(file) => 
                    {
                        active_file = Some(file)
                    },
                    Err(e) => 
                    {
                        active_file = None;
                        println!("{:?}",e)
                    },
                }


                // active_file = new_file.ok();
            }

            if let Some(ref mut file) = active_file
            {
                // println!("Adding record wiht {} last was {:?}", date, current_date);
                if let Some(last_recorded_time) = current_date
                {
                    if date > last_recorded_time
                    {
                        // println!("add and update date");
                        current_date = Some(date);
                        let _r =writeln!(file, "{line}");

                        let mut lock = state.last_sample.lock().await;
                        lock.insert(sensor_id, line.to_string());

                    }
                    else
                    {
                        // println!("excluding");
                    }
                }
                else
                {
                    current_date = Some(date);
                    let _r = writeln!(file, "{line}");

                    let mut lock = state.last_sample.lock().await;
                    lock.insert(sensor_id, line.to_string());
                }
            }
            else
            {
                println!("Error with file {:?}", current_file)
            }
        }
        else
        {
            println!("Error receiving data from sensor station, try to continue reading the file");
            // return "ok".to_owned();
        }
    }

    return "ok".to_owned();
}

async fn handle_get(State(state): State<AppState>, Query(params): Query<GetParams>) -> String 
{
    println!("HandleGet:requested data for sensor {} with Date: {}", params.sensor, params.date);
    let path_string = format!("data/{}_{}.csv",params.sensor, params.date);
    let all_data = get_data(&path_string);
    all_data
}

const CSV_HEADER: &str = "version,sensor_id,datetime,t0,t1,t2,humidity,ambient_temp";

async fn handle_get_csv(Query(params): Query<GetParams>) -> impl axum::response::IntoResponse
{
    println!("HandleGetCsv:requested csv for sensor {} with Date: {}", params.sensor, params.date);
    let path_string = format!("data/{}_{}.csv", params.sensor, params.date);
    let all_data = get_data(&path_string);
    let body = format!("{}\n{}", CSV_HEADER, all_data);
    (
        [
            (axum::http::header::CONTENT_TYPE, "text/csv; charset=utf-8".to_string()),
            (
                axum::http::header::CONTENT_DISPOSITION,
                format!("inline; filename=\"{}_{}.csv\"", params.sensor, params.date),
            ),
        ],
        body,
    )
}

async fn find_latest_sample_for_sensor(sensor: &str) -> Option<String>
{
    let mut read_dir = tokio::fs::read_dir("data").await.ok()?;
    let prefix = format!("{}_", sensor);
    let mut latest_file: Option<String> = None;

    while let Ok(Some(entry)) = read_dir.next_entry().await
    {
        let name = entry.file_name();
        let name_str = name.to_string_lossy();
        if name_str.starts_with(&prefix) && name_str.ends_with(".csv")
        {
            if latest_file.as_deref().map_or(true, |cur| name_str.as_ref() > cur)
            {
                latest_file = Some(name_str.into_owned());
            }
        }
    }

    let file_name = latest_file?;
    println!("HandleGetReport: reading latest file {}", file_name);
    let content = tokio::fs::read_to_string(format!("data/{}", file_name)).await.ok()?;
    content.lines().last().map(|s| s.to_string())
}

async fn handle_get_report(State(state): State<AppState>, Query(params) : Query<GetReportParams>) -> String
{
    println!("HandleGetReport:requested report for sensor {}", params.sensor);
    {
        let samples_lock = state.last_sample.lock().await;
        if let Some(last_sample) = samples_lock.get(&params.sensor)
        {
            return last_sample.clone();
        }
    }

    println!("HandleGetReport: state empty, searching latest file for {}", params.sensor);
    if let Some(last_line) = find_latest_sample_for_sensor(&params.sensor).await
    {
        return last_line;
    }

    format!("{},error", params.sensor)
}

// Parses "version,sensor_id,temperature,humidity,uptime_s" (version 01)
fn parse_health(line: &str) -> Option<DeviceHealth>
{
    let parts: Vec<&str> = line.trim().split(',').map(|p| p.trim()).collect();
    if parts.len() != 5 || parts[0] != "01" || parts[1].is_empty()
    {
        return None;
    }
    let uptime_s: u64 = parts[4].parse().ok()?;

    let now = Utc::now();
    let uptime = chrono::Duration::try_seconds(i64::try_from(uptime_s).ok()?)?;
    let powered_on_at = (now - uptime).to_rfc3339();

    Some(DeviceHealth
    {
        version: parts[0].to_string(),
        sensor_id: parts[1].to_string(),
        temperature: parts[2].parse().ok()?,
        humidity: parts[3].parse().ok()?,
        uptime_s,
        powered_on_at,
        received_at: now.to_rfc3339(),
    })
}

async fn handle_post_health(State(state): State<AppState>, payload: String) -> (axum::http::StatusCode, String)
{
    println!("HandlePostHealth: received {}", payload.trim());
    match parse_health(&payload)
    {
        Some(health) =>
        {
            let mut lock = state.last_health.lock().await;
            lock.insert(health.sensor_id.clone(), health);
            (axum::http::StatusCode::OK, "ok".to_owned())
        },
        None => (axum::http::StatusCode::BAD_REQUEST, "invalid health format, expected version,sensor_id,temperature,humidity,uptime_s".to_owned()),
    }
}

async fn handle_get_health(State(state): State<AppState>, Query(params): Query<GetHealthParams>) -> Result<Json<DeviceHealth>, axum::http::StatusCode>
{
    println!("HandleGetHealth: requested health for sensor {}", params.sensor);
    let lock = state.last_health.lock().await;
    lock.get(&params.sensor).cloned().map(Json).ok_or(axum::http::StatusCode::NOT_FOUND)
}

#[tokio::main]
async fn main() 
{

    let shared_state = AppState 
    {
        last_sample: Arc::new(Mutex::new(HashMap::new())),
        last_health: Arc::new(Mutex::new(HashMap::new())),
    };


    // tokio::spawn(async move 
    // {
    //     let app = Router::new()
    //     .nest_service("/tracking", axum::routing::get_service(ServeDir::new("public")));
    //     let http_listener = TcpListener::bind("0.0.0.0:80").await.unwrap();
    //     axum::serve(http_listener, app).await.unwrap();
    // });

    // Build our router
    let app = Router::new()
        .nest_service("/tracking", axum::routing::get_service(ServeDir::new("public")))
        .route("/api/monitor/samples", post(handle_post))
        .route("/samples", post(handle_post))
        .route("/api/monitor/health", post(handle_post_health).get(handle_get_health))
        .route("/samples", get(handle_get))
        .route("/samples_csv", get(handle_get_csv))
        .route("/report", get(handle_get_report)).with_state(shared_state);


    println!("running server");

    let listener = TcpListener::bind("0.0.0.0:21001").await.unwrap();
    axum::serve(listener, app).await.unwrap();

#[cfg(test)]
mod tests {
    use super::*;  // Import parent module

    #[test]
    fn test_add() {
        let test_date = "2025-10-20T16:24:31.343795-06:00";
        let dt_with_tz = DateTime::parse_from_rfc3339(test_date);
        assert!(true)
    }



    // SENSOR_ID, datetime, t0, t1, t2, t3, humidity, dht_temp
}}