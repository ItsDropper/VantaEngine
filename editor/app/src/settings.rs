use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct EditorSettings {
    pub recent_projects: Vec<String>,
    pub accent: String,
    pub compact_mode: bool,
}

impl Default for EditorSettings {
    fn default() -> Self {
        Self { recent_projects: Vec::new(), accent: "blue".into(), compact_mode: false }
    }
}

impl EditorSettings {
    pub fn load() -> Self {
        settings_path().and_then(|path| fs::read_to_string(path).ok())
            .and_then(|content| serde_json::from_str(&content).ok())
            .unwrap_or_default()
    }

    pub fn remember_project(&mut self, path: &Path) {
        self.push_project(path);
        self.save();
    }

    fn push_project(&mut self, path: &PathBuf) {
        let value = path.to_string_lossy().into_owned();
        self.recent_projects.retain(|item| item != &value);
        self.recent_projects.insert(0, value);
        self.recent_projects.truncate(8);
    }

    pub fn save(&self) {
        let Some(path) = settings_path() else { return; };
        let Some(parent) = path.parent() else { return; };
        if fs::create_dir_all(parent).is_err() { return; }
        let Ok(content) = serde_json::to_string_pretty(self) else { return; };
        let temporary = path.with_extension("json.tmp");
        if fs::write(&temporary, content).is_ok() {
            if fs::rename(&temporary, &path).is_err() {
                let _ = fs::remove_file(&path);
                let _ = fs::rename(&temporary, &path);
            }
        }
    }
}

fn settings_path() -> Option<PathBuf> {
    #[cfg(target_os = "windows")]
    let base = std::env::var_os("APPDATA").map(PathBuf::from);
    #[cfg(target_os = "macos")]
    let base = std::env::var_os("HOME").map(|home| PathBuf::from(home).join("Library").join("Application Support"));
    #[cfg(all(unix, not(target_os = "macos")))]
    let base = std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|home| PathBuf::from(home).join(".config")));
    base.map(|directory| directory.join("VantaEngine").join("editor.json"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn recent_projects_are_deduplicated_and_bounded() {
        let mut settings = EditorSettings::default();
        for index in 0..10 {
            settings.push_project(&PathBuf::from(format!("project-{index}")));
        }
        settings.push_project(&PathBuf::from("project-4"));
        assert_eq!(settings.recent_projects[0], "project-4");
        assert_eq!(settings.recent_projects.len(), 8);
    }

    #[test]
    fn settings_default_when_no_configuration_exists() {
        let settings = EditorSettings::default();
        assert_eq!(settings.accent, "blue");
        assert!(!settings.compact_mode);
        assert!(settings.recent_projects.is_empty());
    }
}
