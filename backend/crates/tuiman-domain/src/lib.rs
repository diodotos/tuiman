use serde::{Deserialize, Serialize};

pub const TUIMAN_VERSION: &str = env!("CARGO_PKG_VERSION");

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct Request {
    pub id: String,
    pub name: String,
    pub method: String,
    pub url: String,
    pub header_key: String,
    pub header_value: String,
    pub body: String,
    pub auth_type: String,
    pub auth_secret_ref: String,
    pub auth_key_name: String,
    pub auth_location: String,
    pub auth_username: String,
    pub updated_at: String,
}

impl Request {
    pub fn empty() -> Self {
        Self {
            id: String::new(),
            name: "New Request".to_string(),
            method: "GET".to_string(),
            url: String::new(),
            header_key: String::new(),
            header_value: String::new(),
            body: String::new(),
            auth_type: "none".to_string(),
            auth_secret_ref: String::new(),
            auth_key_name: String::new(),
            auth_location: String::new(),
            auth_username: String::new(),
            updated_at: String::new(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct RunEntry {
    pub id: i64,
    pub request_id: String,
    pub request_name: String,
    pub method: String,
    pub url: String,
    pub status_code: i64,
    pub duration_ms: i64,
    pub error: String,
    pub created_at: String,
    pub request_snapshot: String,
    pub response_body: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct AppPaths {
    pub config_dir: String,
    pub state_dir: String,
    pub cache_dir: String,
    pub requests_dir: String,
    pub history_db: String,
}
