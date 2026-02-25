use serde::{Deserialize, Serialize};
use tuiman_domain::{Request, RunEntry};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RpcEnvelope {
    pub id: u64,
    pub method: String,
    #[serde(default)]
    pub params: serde_json::Value,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RpcResponse {
    pub id: u64,
    pub ok: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub result: Option<serde_json::Value>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BootstrapPayload {
    pub requests: Vec<Request>,
    pub runs: Vec<RunEntry>,
}

pub fn ok_response<T: Serialize>(id: u64, result: &T) -> RpcResponse {
    RpcResponse {
        id,
        ok: true,
        result: Some(serde_json::to_value(result).unwrap_or(serde_json::Value::Null)),
        error: None,
    }
}

pub fn error_response(id: u64, message: impl Into<String>) -> RpcResponse {
    RpcResponse {
        id,
        ok: false,
        result: None,
        error: Some(message.into()),
    }
}
