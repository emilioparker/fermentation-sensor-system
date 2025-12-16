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

fn extract_data(line : &str) -> Option<(String, String, DateTime<FixedOffset>, f32, f32, f32)>
{
    let mut splitted_data = line.split(',');

    let sensor_id = splitted_data.next().unwrap_or("ufo").to_string();
    let date = splitted_data.next().unwrap_or("");
    // println!("record with date ({:?})", date);

    let dt_with_tz = DateTime::parse_from_rfc3339(date);

    match dt_with_tz 
    {
        Ok(parsed_date) => 
        {
            let date_only = parsed_date.format("%Y-%m-%d").to_string();

            let temp_a: f32 = splitted_data.next().unwrap().parse().unwrap(); // or f64
            let temp_b: f32 = splitted_data.next().unwrap().parse().unwrap(); // or f64
            let temp_c: f32 = splitted_data.next().unwrap().parse().unwrap(); // or f64


            return Some((date_only ,sensor_id, parsed_date, temp_a, temp_b, temp_c));
        },
        Err(error) => 
        {
            println!("{:?}", error);
            None
        },
    }

    // let dt_utc: DateTime<Utc> = dt_with_tz.with_timezone(&Utc);

    // let dt: DateTime<Utc> = date.parse().unwrap();
    // Format to only include the date
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
                if let Some((_date_only, sensor_id, date, _temp_a, _temp_b, _temp_c)) = extract_data(last_line)
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

fn get_last_date_recorded(path_string : &str) -> Option<DateTime<FixedOffset>>
{
    let contents = fs::read_to_string(path_string);
    if let Ok(content) = contents
    {
        if let Some(last_line) = content.lines().last()
        {
            println!("Last line: {}", last_line);
            if let Some((date_only, sensor_id, date, temp_a, temp_b, temp_c)) = extract_data(last_line)
            {
                return Some(date);
            }
            else {
                println!("Error decoding data get last date recorded");
                return None;
            }

        }
        else
        {
            println!("File is empty");
            return None;
        }
    }
    else 
    {
        println!("file not found");
        return None;
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

    let lines = payload.lines();
    for line in lines
    {
        if let Some((date_only, sensor_id, date, _temp_a, _temp_b, _temp_c)) = extract_data(line)
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
    println!("requested data for sensor {} with Date: {}", params.sensor, params.date);
    let path_string = format!("data/{}_{}.csv",params.sensor, params.date);
    let all_data = get_data(&path_string);
    all_data
}

async fn handle_get_report(State(state): State<AppState>, Query(params) : Query<GetReportParams>) -> String 
{
    println!("requested report for sensor {}", params.sensor);
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

    let listener = TcpListener::bind("0.0.0.0:8080").await.unwrap();
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
}}