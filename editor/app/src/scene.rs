use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Transform {
    pub position: [f32; 3],
    pub rotation: [f32; 3],
    pub scale: [f32; 3],
}

impl Default for Transform {
    fn default() -> Self {
        Self { position: [0.0, 0.0, 0.0], rotation: [0.0, 0.0, 0.0], scale: [1.0, 1.0, 1.0] }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct SceneEntity {
    pub id: u64,
    pub name: String,
    pub kind: String,
    #[serde(default)]
    pub transform: Transform,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Scene {
    pub schema_version: u32,
    pub name: String,
    #[serde(default)]
    pub entities: Vec<SceneEntity>,
    #[serde(default)]
    next_entity_id: u64,
}

impl Scene {
    pub fn open(path: &Path) -> Result<Self, String> {
        if !path.exists() {
            let scene = Self::new("Main Scene");
            scene.save(path)?;
            return Ok(scene);
        }
        let content = fs::read_to_string(path)
            .map_err(|error| format!("Cannot read scene {}: {error}", path.display()))?;
        let scene: Self = serde_json::from_str(&content)
            .map_err(|error| format!("Invalid scene file: {error}"))?;
        if scene.schema_version != 1 {
            return Err(format!("Unsupported scene schema version: {}", scene.schema_version));
        }
        if scene.name.trim().is_empty() {
            return Err("Scene name cannot be empty.".into());
        }
        if scene.entities.iter().any(|entity| entity.name.trim().is_empty()) {
            return Err("Every scene entity must have a name.".into());
        }
        Ok(scene)
    }

    pub fn new(name: impl Into<String>) -> Self {
        Self { schema_version: 1, name: name.into(), entities: Vec::new(), next_entity_id: 1 }
    }

    pub fn add_entity(&mut self, name: impl Into<String>, kind: impl Into<String>) -> u64 {
        let id = self.next_entity_id.max(self.entities.iter().map(|entity| entity.id).max().unwrap_or(0) + 1);
        self.next_entity_id = id + 1;
        self.entities.push(SceneEntity { id, name: name.into(), kind: kind.into(), transform: Transform::default() });
        id
    }

    pub fn remove_entity(&mut self, id: u64) -> bool {
        let before = self.entities.len();
        self.entities.retain(|entity| entity.id != id);
        before != self.entities.len()
    }

    pub fn duplicate_entity(&mut self, id: u64) -> Option<u64> {
        let mut entity = self.entities.iter().find(|entity| entity.id == id)?.clone();
        let new_id = self.next_entity_id.max(self.entities.iter().map(|item| item.id).max().unwrap_or(0) + 1);
        self.next_entity_id = new_id + 1;
        entity.id = new_id;
        entity.name = format!("{} Copy", entity.name);
        entity.transform.position[0] += 1.0;
        self.entities.push(entity);
        Some(new_id)
    }

    pub fn save(&self, path: &Path) -> Result<(), String> {
        let parent = path.parent().ok_or_else(|| "Scene path has no parent directory.".to_owned())?;
        fs::create_dir_all(parent).map_err(|error| format!("Cannot create scene directory: {error}"))?;
        let serialized = serde_json::to_string_pretty(self)
            .map_err(|error| format!("Cannot serialize scene: {error}"))?;
        let temporary = temporary_path(path);
        fs::write(&temporary, format!("{serialized}\\n"))
            .map_err(|error| format!("Cannot write scene: {error}"))?;
        match fs::rename(&temporary, path) {
            Ok(()) => Ok(()),
            Err(first_error) => {
                if path.exists() {
                    fs::remove_file(path).map_err(|error| {
                        let _ = fs::remove_file(&temporary);
                        format!("Cannot replace scene file ({first_error}): {error}")
                    })?;
                }
                fs::rename(&temporary, path)
                    .map_err(|error| format!("Cannot finalize scene save: {error}"))
            }
        }
    }
}

fn temporary_path(path: &Path) -> PathBuf {
    let mut name = path.file_name().unwrap_or_default().to_os_string();
    name.push(format!(".{}.tmp", std::process::id()));
    path.with_file_name(name)
}

pub fn scene_path(project_root: &Path) -> PathBuf {
    project_root.join("scenes").join("Main.vanta-scene.json")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn scene_round_trip_preserves_entities_and_transforms() {
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("scenes/main.vanta-scene.json");
        let mut scene = Scene::new("Test");
        let id = scene.add_entity("Ground", "StaticMesh");
        scene.entities[0].transform.position = [1.0, 2.0, 3.0];
        scene.save(&path).unwrap();
        let loaded = Scene::open(&path).unwrap();
        assert_eq!(loaded.name, "Test");
        assert_eq!(loaded.entities[0].id, id);
        assert_eq!(loaded.entities[0].transform.position, [1.0, 2.0, 3.0]);
    }

    #[test]
    fn duplicate_and_remove_entities_keep_unique_ids() {
        let mut scene = Scene::new("Test");
        let original = scene.add_entity("Cube", "StaticMesh");
        let copy = scene.duplicate_entity(original).unwrap();
        assert_ne!(original, copy);
        assert_eq!(scene.entities.len(), 2);
        assert!(scene.remove_entity(original));
        assert_eq!(scene.entities[0].id, copy);
    }

    #[test]
    fn rejects_unknown_scene_schema() {
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("scene.json");
        fs::write(&path, r#"{"schema_version":9,"name":"Nope"}"#).unwrap();
        assert!(Scene::open(&path).unwrap_err().contains("Unsupported scene schema"));
    }
}
