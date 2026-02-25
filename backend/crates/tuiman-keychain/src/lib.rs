use std::process::Command;

const SERVICE: &str = "tuiman";

pub fn get_secret(secret_ref: &str) -> anyhow::Result<String> {
    let output = Command::new("/usr/bin/security")
        .args([
            "find-generic-password",
            "-a",
            secret_ref,
            "-s",
            SERVICE,
            "-w",
        ])
        .output()?;

    if !output.status.success() {
        anyhow::bail!("keychain secret not found for {}", secret_ref);
    }

    Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
}
