use serde::{Deserialize, Serialize};
use tuiman_domain::Request;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HttpResponse {
    pub status_code: i64,
    pub duration_ms: i64,
    pub body: String,
    pub error: String,
}

pub fn send_request(_request: &Request) -> anyhow::Result<HttpResponse> {
    Ok(HttpResponse {
        status_code: 0,
        duration_ms: 0,
        body: String::new(),
        error: "HTTP client not implemented yet in Rust backend".to_string(),
    })
}
