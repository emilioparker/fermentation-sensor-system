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

#[derive(Clone)]
struct AppState 
{
    last_sample: Arc<tokio::sync::Mutex<HashMap<String, String>>>,
}

fn extract_data(line: &str) -> Option<(String, String, DateTime<FixedOffset>, f32, f32, f32, f32, f32, f32)>
{
    let mut parts = line.split(',');

    let first = parts.next().unwrap_or("").trim();
    let is_new_format = first == "01";
    println!("data version: {}", if is_new_format { first } else { "legacy" });

    let sensor_id: String;
    let date_str: &str;
    let t0: f32;
    let t1: f32;
    let t2: f32;
    let t3: f32;
    let humidity: f32;
    let dht_temp: f32;

    if is_new_format
    {
        sensor_id = parts.next().unwrap_or("ufo").trim().to_string();
        date_str = parts.next().unwrap_or("");
        t0       = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
        t1       = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
        t2       = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
        t3       = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
        humidity = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
        dht_temp = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
    }
    else
    {
        sensor_id = first.to_string();
        date_str  = parts.next().unwrap_or("");
        t0        = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
        t1        = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
        t2        = parts.next().unwrap_or("0").trim().parse().unwrap_or(0.0);
        t3        = 0.0;
        humidity  = 0.0;
        dht_temp  = 0.0;
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
    // println!("{}", payload);

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

async fn handle_get_report(State(state): State<AppState>, Query(params) : Query<GetReportParams>) -> String 
{
    println!("HandleGetReport:requested report for sensor {}", params.sensor);
    let samples_lock = state.last_sample.lock().await;
    if let Some(last_sample)  = samples_lock.get(&params.sensor)
    {
        let data = format!("{last_sample}");
        return data;
    }
    else 
    {
        let data = format!("{},error", params.sensor);
        return data;
    }

}

#[tokio::main]
async fn main() 
{

    let shared_state = AppState 
    {
        last_sample: Arc::new(Mutex::new(HashMap::new())),
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
        .route("/samples", post(handle_post))
        .route("/samples", get(handle_get))
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