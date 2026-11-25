use serde::Deserialize;
use std::fs;
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, Deserialize, Default)]
pub struct GeometryStats {
    pub mesh_count: usize,
    pub vertex_count: usize,
    pub triangle_count: usize,
    pub material_count: usize,
}

#[derive(Debug, Clone, Deserialize, Default)]
pub struct AssetMetadata {
    pub schema_version: u32,
    pub model: String,
    #[serde(default)]
    pub geometry: GeometryStats,
    #[serde(default)]
    pub files: std::collections::BTreeMap<String, String>,
}

#[derive(Debug, Clone)]
pub struct AssetRecord {
    pub metadata_path: PathBuf,
    pub metadata: AssetMetadata,
}

pub fn discover_assets(output: &Path) -> Vec<AssetRecord> {
    let Ok(entries) = fs::read_dir(output) else { return Vec::new(); };
    let mut assets = entries
        .filter_map(Result::ok)
        .map(|entry| entry.path())
        .filter(|path| path.file_name().and_then(|value| value.to_str())
            .is_some_and(|name| name.ends_with(".vanta-model.json")))
        .filter_map(|metadata_path| {
            let content = fs::read_to_string(&metadata_path).ok()?;
            let metadata: AssetMetadata = serde_json::from_str(&content).ok()?;
            Some(AssetRecord { metadata_path, metadata })
        })
        .collect::<Vec<_>>();
    assets.sort_by(|left, right| left.metadata.model.cmp(&right.metadata.model));
    assets
}

pub fn project_files(root: &Path) -> Vec<(PathBuf, bool)> {
    fn visit(directory: &Path, root: &Path, depth: usize, output: &mut Vec<(PathBuf, bool)>) {
        if depth > 3 || output.len() >= 180 { return; }
        let Ok(entries) = fs::read_dir(directory) else { return; };
        let mut entries = entries.filter_map(Result::ok).collect::<Vec<_>>();
        entries.sort_by_key(|entry| entry.file_name());
        for entry in entries {
            if output.len() >= 180 { break; }
            let path = entry.path();
            let name = entry.file_name();
            let name = name.to_string_lossy();
            if name.starts_with('.') || matches!(name.as_ref(), "build" | "target" | "__pycache__" | "node_modules") {
                continue;
            }
            let is_directory = entry.file_type().map(|kind| kind.is_dir()).unwrap_or(false);
            output.push((path.strip_prefix(root).unwrap_or(&path).to_path_buf(), is_directory));
            if is_directory { visit(&path, root, depth + 1, output); }
        }
    }
    let mut files = Vec::new();
    visit(root, root, 0, &mut files);
    files
}
