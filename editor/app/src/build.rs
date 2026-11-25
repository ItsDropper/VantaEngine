use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Debug, Clone)]
pub struct BuildResult {
    pub success: bool,
    pub output: String,
}

pub fn build_project(project: &Path, output: &Path, repository_root: &Path) -> BuildResult {
    let sdk_path = std::env::var_os("VANTA_SDK_PATH")
        .map(PathBuf::from)
        .unwrap_or_else(|| repository_root.join("tools").join("vanta_sdk"));
    let python = std::env::var_os("VANTA_PYTHON").unwrap_or_else(|| {
        if cfg!(windows) { "python".into() } else { "python3".into() }
    });
    let mut search_path = sdk_path.as_os_str().to_owned();
    if let Some(existing) = std::env::var_os("PYTHONPATH") {
        search_path.push(if cfg!(windows) { ";" } else { ":" });
        search_path.push(existing);
    }
    let result = Command::new(python)
        .arg("-m").arg("vanta_sdk").arg("build")
        .arg("--project").arg(project).arg("--output").arg(output)
        .env("PYTHONPATH", search_path)
        .current_dir(project)
        .output();
    match result {
        Ok(result) => {
            let mut output_text = String::from_utf8_lossy(&result.stdout).into_owned();
            let stderr = String::from_utf8_lossy(&result.stderr);
            if !stderr.trim().is_empty() {
                if !output_text.is_empty() { output_text.push('\n'); }
                output_text.push_str(&stderr);
            }
            if output_text.trim().is_empty() {
                output_text = if result.status.success() {
                    "Asset build completed.".into()
                } else {
                    format!("Asset build failed with status {}.", result.status)
                };
            }
            BuildResult { success: result.status.success(), output: output_text }
        }
        Err(error) => BuildResult {
            success: false,
            output: format!("Could not start Python ({error}). Install Python 3.12+ or set VANTA_PYTHON."),
        },
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn missing_project_directory_fails_without_panicking() {
        let directory = tempfile::tempdir().unwrap();
        let result = build_project(
            &directory.path().join("missing"),
            &directory.path().join("out"),
            directory.path(),
        );
        assert!(!result.success);
        assert!(!result.output.is_empty());
    }
}
