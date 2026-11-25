use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::thread;
use std::time::{Duration, Instant};

const BUILD_TIMEOUT: Duration = Duration::from_secs(300);

#[derive(Debug, Clone)]
pub struct BuildResult {
    pub success: bool,
    pub output: String,
}

pub fn build_project(project: &Path, output: &Path, repository_root: &Path) -> BuildResult {
    let sdk_path = resolve_sdk_path(repository_root);
    let python = std::env::var_os("VANTA_PYTHON").unwrap_or_else(|| {
        if cfg!(windows) { "python".into() } else { "python3".into() }
    });
    let mut search_path = sdk_path.as_os_str().to_owned();
    if let Some(existing) = std::env::var_os("PYTHONPATH") {
        search_path.push(if cfg!(windows) { ";" } else { ":" });
        search_path.push(existing);
    }

    let mut child = match Command::new(python)
        .arg("-m").arg("vanta_sdk").arg("build")
        .arg("--project").arg(project).arg("--output").arg(output)
        .env("PYTHONPATH", search_path)
        .current_dir(project)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
    {
        Ok(child) => child,
        Err(error) => return BuildResult {
            success: false,
            output: format!("Could not start Python ({error}). Install Python 3.12+ or set VANTA_PYTHON."),
        },
    };

    let started = Instant::now();
    loop {
        match child.try_wait() {
            Ok(Some(_)) => break,
            Ok(None) if started.elapsed() < BUILD_TIMEOUT => thread::sleep(Duration::from_millis(100)),
            Ok(None) => {
                let _ = child.kill();
                let output = child.wait_with_output();
                let detail = output.map(|result| combine_output(&result.stdout, &result.stderr))
                    .unwrap_or_default();
                return BuildResult {
                    success: false,
                    output: append_message(detail, "Asset build timed out after five minutes."),
                };
            }
            Err(error) => {
                let _ = child.kill();
                let _ = child.wait();
                return BuildResult {
                    success: false,
                    output: format!("Could not monitor asset build process: {error}"),
                };
            }
        }
    }

    match child.wait_with_output() {
        Ok(result) => {
            let mut output_text = combine_output(&result.stdout, &result.stderr);
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
            output: format!("Could not collect asset build output: {error}"),
        },
    }
}

fn resolve_sdk_path(repository_root: &Path) -> PathBuf {
    if let Some(path) = std::env::var_os("VANTA_SDK_PATH") {
        return PathBuf::from(path);
    }
    let executable_directory = std::env::current_exe().ok()
        .and_then(|path| path.parent().map(Path::to_path_buf));
    executable_directory.as_deref()
        .and_then(bundled_sdk_path)
        .unwrap_or_else(|| repository_root.join("tools").join("vanta_sdk"))
}

fn bundled_sdk_path(executable_directory: &Path) -> Option<PathBuf> {
    [
        executable_directory.join("resources").join("vanta_sdk"),
        executable_directory.join("tools").join("vanta_sdk"),
    ].into_iter().find(|candidate| candidate.join("vanta_sdk").is_dir())
}

fn combine_output(stdout: &[u8], stderr: &[u8]) -> String {
    let mut output = String::from_utf8_lossy(stdout).into_owned();
    let error_output = String::from_utf8_lossy(stderr);
    if !error_output.trim().is_empty() {
        if !output.is_empty() { output.push('\n'); }
        output.push_str(&error_output);
    }
    output
}

fn append_message(mut output: String, message: &str) -> String {
    if !output.is_empty() { output.push('\n'); }
    output.push_str(message);
    output
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

    #[test]
    fn packaged_sdk_is_discovered_next_to_the_executable() {
        let directory = tempfile::tempdir().unwrap();
        let executable_directory = directory.path().join("bin");
        let sdk = executable_directory.join("resources").join("vanta_sdk");
        std::fs::create_dir_all(sdk.join("vanta_sdk")).unwrap();
        assert_eq!(bundled_sdk_path(&executable_directory), Some(sdk));
    }

    #[test]
    fn combines_stdout_and_stderr_without_losing_error_output() {
        let output = combine_output(b"build started", b"warning: sample");
        assert!(output.contains("build started"));
        assert!(output.contains("warning: sample"));
    }
}
