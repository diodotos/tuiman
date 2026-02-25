use std::fs;
use std::path::Path;

use anyhow::Context;
use tuiman_domain::{Request, RunEntry};

pub fn load_requests_from_dir(path: &Path) -> anyhow::Result<Vec<Request>> {
    if !path.exists() {
        return Ok(Vec::new());
    }

    let mut requests = Vec::new();
    for entry in fs::read_dir(path).with_context(|| format!("failed reading {}", path.display()))? {
        let entry = entry?;
        let file_type = entry.file_type()?;
        if !file_type.is_file() {
            continue;
        }

        let p = entry.path();
        if p.extension().and_then(|e| e.to_str()) != Some("json") {
            continue;
        }

        let raw = fs::read_to_string(&p)
            .with_context(|| format!("failed reading request file {}", p.display()))?;
        let req = serde_json::from_str::<Request>(&raw)
            .with_context(|| format!("invalid request json {}", p.display()))?;
        requests.push(req);
    }

    requests.sort_by(|a, b| a.name.to_lowercase().cmp(&b.name.to_lowercase()));
    Ok(requests)
}

pub fn load_recent_runs(_history_db: &Path, _limit: usize) -> anyhow::Result<Vec<RunEntry>> {
    Ok(Vec::new())
}
