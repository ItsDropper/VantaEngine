use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProjectManifest {
    pub schema_version: u32,
    pub name: String,
    pub entrypoint: String,
}

#[derive(Debug, Clone)]
pub struct Project {
    pub root: PathBuf,
    pub name: String,
    pub entrypoint: PathBuf,
    pub output: PathBuf,
}

impl Project {
    pub fn open(directory: impl AsRef<Path>) -> Result<Self, String> {
        let root = directory.as_ref().canonicalize()
            .map_err(|error| format!("Cannot resolve project directory: {error}"))?;
        let manifest_path = root.join("vanta.project.json");
        let manifest_text = fs::read_to_string(&manifest_path)
            .map_err(|error| format!("Cannot read {}: {error}", manifest_path.display()))?;
        let manifest: ProjectManifest = serde_json::from_str(&manifest_text)
            .map_err(|error| format!("Invalid project manifest: {error}"))?;
        if manifest.schema_version != 1 {
            return Err(format!("Unsupported project schema version: {}", manifest.schema_version));
        }
        if manifest.name.trim().is_empty() || manifest.entrypoint.trim().is_empty() {
            return Err("Project name and entrypoint must be non-empty.".into());
        }
        let entrypoint = root.join(&manifest.entrypoint).canonicalize()
            .map_err(|error| format!("Cannot resolve project entrypoint: {error}"))?;
        if !entrypoint.starts_with(&root) {
            return Err("Project entrypoint must stay inside the project directory.".into());
        }
        if entrypoint.extension().and_then(|value| value.to_str()) != Some("py") || !entrypoint.is_file() {
            return Err("Project entrypoint must be an existing Python file.".into());
        }
        Ok(Self {
            output: root.join("build").join("editor-assets"),
            root,
            name: manifest.name,
            entrypoint,
        })
    }
}

pub fn create_project(directory: impl AsRef<Path>) -> Result<Project, String> {
    let root = directory.as_ref();
    if root.exists() && fs::read_dir(root)
        .map_err(|error| format!("Cannot inspect destination: {error}"))?
        .next().is_some()
    {
        return Err(format!("Destination is not empty: {}", root.display()));
    }
    fs::create_dir_all(root).map_err(|error| format!("Cannot create project directory: {error}"))?;
    let raw_name = root.file_name().and_then(|name| name.to_str()).unwrap_or("VantaProject");
    let name: String = raw_name.chars().map(|character| {
        if character.is_alphanumeric() || character == '_' || character == '-' { character } else { '_' }
    }).collect();
    let name = if name.is_empty() {
        "VantaProject".to_owned()
    } else if !name.chars().next().is_some_and(|character| character.is_alphabetic() || character == '_') {
        format!("V{name}")
    } else {
        name
    };
    let manifest = ProjectManifest { schema_version: 1, name, entrypoint: "generate.py".into() };
    let serialized = serde_json::to_string_pretty(&manifest)
        .map_err(|error| format!("Cannot serialize project manifest: {error}"))?;
    fs::write(root.join("vanta.project.json"), format!("{serialized}\n"))
        .map_err(|error| format!("Cannot write project manifest: {error}"))?;
    fs::write(root.join("generate.py"), STARTER_SCRIPT)
        .map_err(|error| format!("Cannot write starter script: {error}"))?;
    Project::open(root)
}

const STARTER_SCRIPT: &str = r#""""Project entrypoint. Edit build() to author assets with the Vanta SDK."""
from vanta_sdk import Material, Model


def build() -> Model:
    model = Model("starter_asset")
    model.add_material(Material("stone", (0.42, 0.45, 0.48), 0.82, 0.0))
    model.add_box("foundation", (0.0, 0.5, 0.0), (3.0, 1.0, 3.0), "stone")
    model.add_box("upper_block", (0.0, 2.0, 0.0), (2.0, 2.0, 2.0), "stone")
    return model
"#;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn creates_and_reopens_a_project() {
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path().join("New Project");
        let project = create_project(&root).unwrap();
        assert_eq!(project.name, "New_Project");
        assert!(project.entrypoint.is_file());
        assert_eq!(Project::open(&root).unwrap().root, project.root);
    }

    #[test]
    fn rejects_non_empty_destination() {
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path().join("occupied");
        fs::create_dir_all(&root).unwrap();
        fs::write(root.join("keep.txt"), "keep").unwrap();
        assert!(create_project(&root).unwrap_err().contains("not empty"));
        assert_eq!(fs::read_to_string(root.join("keep.txt")).unwrap(), "keep");
    }

    #[test]
    fn rejects_entrypoint_path_traversal() {
        let directory = tempfile::tempdir().unwrap();
        let root = directory.path().join("project");
        fs::create_dir_all(&root).unwrap();
        fs::write(directory.path().join("outside.py"), "pass").unwrap();
        let manifest = ProjectManifest { schema_version: 1, name: "unsafe".into(), entrypoint: "../outside.py".into() };
        fs::write(root.join("vanta.project.json"), serde_json::to_string(&manifest).unwrap()).unwrap();
        assert!(Project::open(&root).unwrap_err().contains("inside the project"));
    }
}
