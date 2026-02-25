use std::env;
use std::io::{self, BufRead, Write};
use std::path::PathBuf;

use anyhow::Context;
use serde_json::json;
use tracing_subscriber::EnvFilter;
use tuiman_domain::{AppPaths, TUIMAN_VERSION};
use tuiman_ipc::{BootstrapPayload, RpcEnvelope, error_response, ok_response};

fn default_paths() -> anyhow::Result<AppPaths> {
    let home = env::var("HOME").context("$HOME is not set")?;
    let config_dir = format!("{home}/.config/tuiman");
    let state_dir = format!("{home}/.local/state/tuiman");
    let cache_dir = format!("{home}/.cache/tuiman");
    let requests_dir = format!("{config_dir}/requests");
    let history_db = format!("{state_dir}/history.db");
    Ok(AppPaths {
        config_dir,
        state_dir,
        cache_dir,
        requests_dir,
        history_db,
    })
}

fn print_help() {
    println!("tuiman-backend {TUIMAN_VERSION}");
    println!("Usage: tuiman-backend [--help] [--version]");
    println!();
    println!("Stdio JSON-RPC methods:");
    println!("  - ping");
    println!("  - bootstrap");
}

fn write_response(resp: &tuiman_ipc::RpcResponse) -> anyhow::Result<()> {
    let mut out = io::stdout().lock();
    serde_json::to_writer(&mut out, resp)?;
    out.write_all(b"\n")?;
    out.flush()?;
    Ok(())
}

fn bootstrap_payload(paths: &AppPaths) -> anyhow::Result<BootstrapPayload> {
    let requests = tuiman_storage::load_requests_from_dir(PathBuf::from(&paths.requests_dir).as_path())?;
    let runs = tuiman_storage::load_recent_runs(PathBuf::from(&paths.history_db).as_path(), 200)?;
    Ok(BootstrapPayload { requests, runs })
}

fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::from_default_env())
        .with_writer(io::stderr)
        .init();

    let args: Vec<String> = env::args().collect();
    if args.len() > 1 {
        match args[1].as_str() {
            "--version" | "-v" => {
                println!("{TUIMAN_VERSION}");
                return Ok(());
            }
            "--help" | "-h" => {
                print_help();
                return Ok(());
            }
            _ => {
                eprintln!("Unknown argument\n");
                print_help();
                return Ok(());
            }
        }
    }

    let paths = default_paths()?;
    let stdin = io::stdin();
    for line in stdin.lock().lines() {
        let line = line?;
        if line.trim().is_empty() {
            continue;
        }

        let parsed = serde_json::from_str::<RpcEnvelope>(&line);
        let envelope = match parsed {
            Ok(value) => value,
            Err(err) => {
                write_response(&error_response(0, format!("invalid request: {err}")))?;
                continue;
            }
        };

        let response = match envelope.method.as_str() {
            "ping" => ok_response(envelope.id, &json!({"version": TUIMAN_VERSION})),
            "bootstrap" => match bootstrap_payload(&paths) {
                Ok(data) => ok_response(envelope.id, &data),
                Err(err) => error_response(envelope.id, format!("bootstrap failed: {err:#}")),
            },
            _ => error_response(envelope.id, format!("unknown method: {}", envelope.method)),
        };

        write_response(&response)?;
    }

    Ok(())
}
