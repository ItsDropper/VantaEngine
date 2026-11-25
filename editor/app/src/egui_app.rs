use crate::assets::{AssetRecord, discover_assets, project_files};
use crate::build::{BuildResult, build_project};
use crate::project::{Project, create_project};
use crate::scene::{Scene, SceneEntity, Transform, scene_path};
use crate::settings::EditorSettings;
use eframe::egui::{self, Align, Color32, Id, Layout, RichText, Sense, Stroke, Vec2, WidgetText};
use egui_dock::{DockArea, DockState, Style, TabViewer};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::mpsc::{self, Receiver};
use std::thread;
use std::time::Duration;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
enum WorkspaceTab {
    Viewport,
    Assets,
    Build,
}

pub struct EditorApp {
    project: Option<Project>,
    assets: Vec<AssetRecord>,
    selected_asset: Option<PathBuf>,
    files: Vec<(PathBuf, bool)>,
    scene: Option<Scene>,
    selected_entity: Option<u64>,
    entity_name_draft: String,
    scene_status: String,
    settings: EditorSettings,
    build_receiver: Option<Receiver<BuildResult>>,
    build_running: bool,
    build_log: String,
    error: Option<String>,
    dock_state: DockState<WorkspaceTab>,
}

impl EditorApp {
    pub fn new(cc: &eframe::CreationContext<'_>) -> Self {
        configure_theme(&cc.egui_ctx);

        #[cfg(target_os = "windows")]
        if let Some(window) = cc.winit_window() {
            if let Err(error) = window_vibrancy::apply_mica(window.as_ref(), Some(true)) {
                eprintln!("VantaEngine Editor: Windows Mica unavailable: {error}");
            }
        }

        #[cfg(target_os = "macos")]
        if let Some(window) = cc.winit_window() {
            if let Err(error) = window_vibrancy::apply_vibrancy(
                window.as_ref(),
                window_vibrancy::NSVisualEffectMaterial::HudWindow,
                None,
                Some(16.0),
            ) {
                eprintln!("VantaEngine Editor: native vibrancy unavailable: {error}");
            }
        }

        let mut app = Self {
            project: None,
            assets: Vec::new(),
            selected_asset: None,
            files: Vec::new(),
            scene: None,
            selected_entity: None,
            entity_name_draft: String::new(),
            scene_status: "No scene loaded".into(),
            settings: EditorSettings::load(),
            build_receiver: None,
            build_running: false,
            build_log: "Open a project to start authoring.".into(),
            error: None,
            dock_state: DockState::new(vec![
                WorkspaceTab::Viewport,
                WorkspaceTab::Assets,
                WorkspaceTab::Build,
            ]),
        };

        if let Some(path) = std::env::var_os("VANTA_EDITOR_PROJECT") {
            match Project::open(PathBuf::from(path)) {
                Ok(project) => app.set_project(project),
                Err(error) => app.error = Some(error),
            }
        }
        app
    }

    fn open_project(&mut self) {
        if let Some(directory) = rfd::FileDialog::new()
            .set_title("Open VantaEngine project")
            .pick_folder()
        {
            match Project::open(directory) {
                Ok(project) => self.set_project(project),
                Err(error) => self.error = Some(error),
            }
        }
    }

    fn new_project(&mut self) {
        if let Some(directory) = rfd::FileDialog::new()
            .set_title("Choose an empty folder for the new project")
            .pick_folder()
        {
            match create_project(&directory) {
                Ok(project) => self.set_project(project),
                Err(error) => self.error = Some(error),
            }
        }
    }

    fn open_recent(&mut self, path: PathBuf) {
        match Project::open(path) {
            Ok(project) => self.set_project(project),
            Err(error) => self.error = Some(error),
        }
    }

    fn set_project(&mut self, project: Project) {
        self.settings.remember_project(&project.root);
        self.project = Some(project);
        self.selected_asset = None;
        self.selected_entity = None;
        self.refresh_project_data();
        self.build_log = "Project opened. Ready to edit the scene or build assets.".into();
    }

    fn refresh_project_data(&mut self) {
        if let Some(project) = &self.project {
            self.assets = discover_assets(&project.output);
            self.files = project_files(&project.root);
            match Scene::open(&scene_path(&project.root)) {
                Ok(scene) => {
                    self.scene_status = format!("{} · {} entities", scene.name, scene.entities.len());
                    self.scene = Some(scene);
                }
                Err(error) => {
                    self.scene = None;
                    self.error = Some(error);
                }
            }
        } else {
            self.assets.clear();
            self.files.clear();
            self.scene = None;
        }
    }

    fn persist_scene(&mut self) {
        let Some(project) = &self.project else { return; };
        let Some(scene) = &self.scene else { return; };
        match scene.save(&scene_path(&project.root)) {
            Ok(()) => {
                self.scene_status = format!("Saved · {} entities", scene.entities.len());
                self.error = None;
            }
            Err(error) => self.error = Some(error),
        }
    }

    fn select_entity(&mut self, id: u64) {
        self.selected_entity = Some(id);
        self.entity_name_draft = self.scene.as_ref()
            .and_then(|scene| scene.entities.iter().find(|entity| entity.id == id))
            .map(|entity| entity.name.clone())
            .unwrap_or_default();
    }

    fn add_entity(&mut self, kind: &'static str) {
        let label = match kind {
            "Camera" => "Camera",
            "PointLight" => "Point Light",
            _ => "Cube",
        };
        let id = self.scene.as_mut().map(|scene| scene.add_entity(label, kind));
        if let Some(id) = id {
            self.selected_entity = Some(id);
            self.entity_name_draft = label.into();
            self.persist_scene();
        }
    }

    fn duplicate_entity(&mut self) {
        let duplicate = self.scene.as_mut()
            .and_then(|scene| self.selected_entity.and_then(|selected| scene.duplicate_entity(selected)));
        if let Some(id) = duplicate {
            self.selected_entity = Some(id);
            self.entity_name_draft = self.scene.as_ref()
                .and_then(|scene| scene.entities.iter().find(|entity| entity.id == id))
                .map(|entity| entity.name.clone())
                .unwrap_or_default();
            self.persist_scene();
        }
    }

    fn delete_entity(&mut self) {
        let removed = self.scene.as_mut()
            .is_some_and(|scene| self.selected_entity.is_some_and(|id| scene.remove_entity(id)));
        if removed {
            self.selected_entity = None;
            self.entity_name_draft.clear();
            self.persist_scene();
        }
    }

    fn start_build(&mut self) {
        let Some(project) = self.project.clone() else { return; };
        if self.build_running { return; }
        let root = repository_root();
        let (sender, receiver) = mpsc::channel();
        match thread::Builder::new().name("vanta-asset-build".into()).spawn(move || {
            let result = build_project(&project.root, &project.output, &root);
            let _ = sender.send(result);
        }) {
            Ok(_) => {
                self.build_receiver = Some(receiver);
                self.build_running = true;
                self.build_log = "Building project assets…".into();
            }
            Err(error) => self.error = Some(format!("Could not start build worker: {error}")),
        }
    }

    fn poll_build(&mut self) {
        let result = self.build_receiver.as_ref().and_then(|receiver| match receiver.try_recv() {
            Ok(result) => Some(Some(result)),
            Err(mpsc::TryRecvError::Disconnected) => Some(None),
            Err(mpsc::TryRecvError::Empty) => None,
        });
        match result {
            Some(Some(result)) => {
                self.build_receiver = None;
                self.build_running = false;
                self.build_log = result.output;
                if result.success {
                    self.refresh_project_data();
                }
            }
            Some(None) => {
                self.build_receiver = None;
                self.build_running = false;
                self.build_log.push_str("\nBuild worker stopped unexpectedly.");
            }
            None => {}
        }
    }

    fn open_file(&self, path: PathBuf) {
        open_in_system_editor(&path);
    }

    fn toolbar(&mut self, ctx: &egui::Context) {
        egui::TopBottomPanel::top("vanta_toolbar")
            .frame(glass_frame(egui::Color32::from_rgba_unmultiplied(20, 29, 46, 220), 14.0))
            .show(ctx, |ui| {
                ui.horizontal(|ui| {
                    ui.vertical(|ui| {
                        ui.label(RichText::new("V A N T A  /  ENGINE").size(10.0).strong().color(accent()));
                        ui.label(RichText::new("Studio").size(22.0).strong().color(Color32::WHITE));
                    });
                    ui.separator();
                    ui.vertical(|ui| {
                        ui.label(RichText::new(self.project.as_ref().map(|p| p.name.as_str()).unwrap_or("NO PROJECT"))
                            .size(12.0).strong());
                        ui.label(RichText::new(self.project.as_ref()
                            .map(|p| p.root.display().to_string())
                            .unwrap_or_else(|| "Open a project to begin".into()))
                            .size(10.0).color(muted()));
                    });
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        let build_label = if self.build_running { "BUILDING…" } else { "BUILD ASSETS" };
                        if ui.add_enabled(self.project.is_some() && !self.build_running,
                            primary_button(build_label)).clicked() {
                            self.start_build();
                        }
                        if ui.add_enabled(self.scene.is_some(), secondary_button("SAVE SCENE")).clicked() {
                            self.persist_scene();
                        }
                        if ui.add(secondary_button("OPEN PROJECT")).clicked() {
                            self.open_project();
                        }
                        if ui.add(secondary_button("NEW PROJECT")).clicked() {
                            self.new_project();
                        }
                    });
                });
            });
    }

    fn left_sidebar(&mut self, ctx: &egui::Context) {
        egui::SidePanel::left("project_sidebar")
            .resizable(true)
            .default_width(258.0)
            .width_range(220.0..=380.0)
            .frame(glass_frame(egui::Color32::from_rgba_unmultiplied(14, 21, 34, 214), 14.0))
            .show(ctx, |ui| {
                ui.add_space(5.0);
                section_label(ui, "PROJECT");
                ui.label(RichText::new(self.project.as_ref().map(|p| p.name.as_str()).unwrap_or("NO PROJECT"))
                    .size(16.0).strong());
                ui.add_space(14.0);

                section_label(ui, "SCENE HIERARCHY");
                if let Some(scene) = &self.scene {
                    ui.label(RichText::new(&scene.name).size(11.0).color(muted()));
                    let entities = scene.entities.iter()
                        .map(|entity| (entity.id, entity.name.clone(), entity.kind.clone()))
                        .collect::<Vec<_>>();
                    if entities.is_empty() {
                        ui.label(RichText::new("No entities yet.").size(11.0).color(muted()));
                    }
                    for (id, name, kind) in entities {
                        let selected = self.selected_entity == Some(id);
                        let glyph = if kind == "Camera" { "◉" } else if kind == "PointLight" { "✦" } else { "◇" };
                        if ui.selectable_label(selected, format!("{glyph}  {name}")).clicked() {
                            self.select_entity(id);
                        }
                    }
                    ui.add_space(8.0);
                    ui.horizontal(|ui| {
                        if ui.small_button("+ Cube").clicked() { self.add_entity("StaticMesh"); }
                        if ui.small_button("+ Camera").clicked() { self.add_entity("Camera"); }
                        if ui.small_button("+ Light").clicked() { self.add_entity("PointLight"); }
                    });
                } else {
                    ui.label(RichText::new("Open a project to edit its scene.").size(11.0).color(muted()));
                }

                ui.add_space(18.0);
                section_label(ui, "PROJECT FILES");
                egui::ScrollArea::vertical().max_height(250.0).show(ui, |ui| {
                    let files = self.files.clone();
                    for (path, is_directory) in files {
                        if is_directory {
                            ui.label(RichText::new(format!("▸  {}", path.display())).size(10.0).color(muted()));
                        } else if ui.small_button(format!("·  {}", path.display())).clicked() {
                            if let Some(project) = &self.project {
                                self.open_file(project.root.join(path));
                            }
                        }
                    }
                });

                ui.add_space(12.0);
                ui.collapsing("Workspace preferences", |ui| {
                    ui.label(RichText::new("ACCENT").size(9.0).strong().color(muted()));
                    for (label, value) in [("Vanta blue", "blue"), ("Mint", "mint"), ("Violet", "violet")] {
                        if ui.radio_value(&mut self.settings.accent, value.into(), label).changed() {
                            self.settings.save();
                        }
                    }
                    if ui.checkbox(&mut self.settings.compact_mode, "Compact density").changed() {
                        self.settings.save();
                    }
                });

                if !self.settings.recent_projects.is_empty() {
                    ui.add_space(12.0);
                    section_label(ui, "RECENT PROJECTS");
                    let recent = self.settings.recent_projects.clone();
                    for path in recent.into_iter().take(5) {
                        let path = PathBuf::from(path);
                        let label = path.file_name().and_then(|v| v.to_str()).unwrap_or("Project").to_owned();
                        if ui.small_button(label).clicked() { self.open_recent(path); }
                    }
                }
            });
    }

    fn right_inspector(&mut self, ctx: &egui::Context) {
        egui::SidePanel::right("inspector_panel")
            .resizable(true)
            .default_width(292.0)
            .width_range(250.0..=420.0)
            .frame(glass_frame(egui::Color32::from_rgba_unmultiplied(14, 21, 34, 214), 14.0))
            .show(ctx, |ui| {
                ui.add_space(5.0);
                section_label(ui, "INSPECTOR");
                let mut dirty = false;
                let mut duplicate = false;
                let mut delete = false;
                if let Some(selected) = self.selected_entity {
                    if let Some(scene) = &mut self.scene {
                        if let Some(entity) = scene.entities.iter_mut().find(|entity| entity.id == selected) {
                            ui.heading(&entity.kind);
                            ui.label(RichText::new(format!("Entity ID  {}", entity.id)).size(10.0).color(muted()));
                            ui.add_space(8.0);
                            ui.label(RichText::new("NAME").size(9.0).strong().color(muted()));
                            ui.text_edit_singleline(&mut self.entity_name_draft);
                            if ui.button("Apply name").clicked() && !self.entity_name_draft.trim().is_empty() {
                                entity.name = self.entity_name_draft.trim().to_owned();
                                dirty = true;
                            }
                            ui.add_space(12.0);
                            transform_group(ui, "POSITION", &mut entity.transform.position, 0.1, &mut dirty);
                            transform_group(ui, "ROTATION", &mut entity.transform.rotation, 1.0, &mut dirty);
                            transform_group(ui, "SCALE", &mut entity.transform.scale, 0.05, &mut dirty);
                        }
                    }
                    ui.add_space(12.0);
                    ui.horizontal(|ui| {
                        duplicate = ui.button("Duplicate").clicked();
                        delete = ui.button("Delete").clicked();
                    });
                } else {
                    ui.add_space(14.0);
                    ui.label(RichText::new("Nothing selected").size(16.0).strong());
                    ui.label(RichText::new("Select an entity in the hierarchy to inspect its properties.")
                        .size(11.0).color(muted()));
                }
                if dirty { self.persist_scene(); }
                if duplicate { self.duplicate_entity(); }
                if delete { self.delete_entity(); }
            });
    }

    fn status_bar(&self, ctx: &egui::Context) {
        egui::TopBottomPanel::bottom("status_bar")
            .frame(glass_frame(egui::Color32::from_rgba_unmultiplied(13, 19, 30, 220), 10.0))
            .show(ctx, |ui| {
                ui.horizontal(|ui| {
                    let (label, color) = if self.build_running {
                        ("BUILD RUNNING", Color32::from_rgb(255, 190, 96))
                    } else if self.error.is_some() {
                        ("ATTENTION", Color32::from_rgb(255, 116, 126))
                    } else {
                        ("READY", Color32::from_rgb(91, 207, 163))
                    };
                    ui.label(RichText::new("●").color(color));
                    ui.label(RichText::new(label).size(9.0).strong().color(color));
                    ui.separator();
                    ui.label(RichText::new(&self.scene_status).size(10.0).color(muted()));
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        ui.label(RichText::new("VANTAENGINE  ·  NATIVE RUST WORKSPACE").size(9.0).color(muted()));
                    });
                });
            });
    }

    fn show_error(&mut self, ctx: &egui::Context) {
        if let Some(error) = self.error.clone() {
            egui::TopBottomPanel::top("error_banner")
                .frame(egui::Frame::new()
                    .fill(Color32::from_rgba_unmultiplied(110, 35, 48, 230))
                    .inner_margin(egui::Margin::same(10)))
                .show(ctx, |ui| {
                    ui.horizontal(|ui| {
                        ui.label(RichText::new(error).color(Color32::WHITE));
                        if ui.button("Dismiss").clicked() { self.error = None; }
                    });
                });
        }
    }
}

impl eframe::App for EditorApp {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();
        self.poll_build();
        if self.build_running {
            ctx.request_repaint_after(Duration::from_millis(100));
        }

        self.toolbar(&ctx);
        self.status_bar(&ctx);
        self.left_sidebar(&ctx);
        self.right_inspector(&ctx);
        self.show_error(&ctx);

        egui::CentralPanel::default()
            .frame(egui::Frame::new()
                .fill(Color32::from_rgba_unmultiplied(8, 13, 22, 118))
                .inner_margin(egui::Margin::same(10)))
            .show(&ctx, |ui| {
                let mut viewer = WorkspaceTabViewer {
                    scene: &self.scene,
                    assets: &self.assets,
                    selected_asset: &mut self.selected_asset,
                    build_log: &self.build_log,
                };
                let dock_style = Style::from_egui(ui.style().as_ref());
                DockArea::new(&mut self.dock_state)
                    .style(dock_style)
                    .show_inside(ui, &mut viewer);
            });
    }

    fn clear_color(&self, _visuals: &egui::Visuals) -> [f32; 4] {
        Color32::from_rgba_unmultiplied(8, 12, 20, 128).to_normalized_gamma_f32()
    }
}

struct WorkspaceTabViewer<'a> {
    scene: &'a Option<Scene>,
    assets: &'a [AssetRecord],
    selected_asset: &'a mut Option<PathBuf>,
    build_log: &'a str,
}

impl TabViewer for WorkspaceTabViewer<'_> {
    type Tab = WorkspaceTab;

    fn id(&mut self, tab: &mut Self::Tab) -> Id {
        Id::new(*tab)
    }

    fn title(&mut self, tab: &mut Self::Tab) -> WidgetText {
        match tab {
            WorkspaceTab::Viewport => "Scene viewport".into(),
            WorkspaceTab::Assets => "Asset library".into(),
            WorkspaceTab::Build => "Build output".into(),
        }
    }

    fn ui(&mut self, ui: &mut egui::Ui, tab: &mut Self::Tab) {
        match tab {
            WorkspaceTab::Viewport => viewport_panel(ui, self.scene.as_ref()),
            WorkspaceTab::Assets => assets_panel(ui, self.assets, self.selected_asset),
            WorkspaceTab::Build => build_panel(ui, self.build_log),
        }
    }
}

fn viewport_panel(ui: &mut egui::Ui, scene: Option<&Scene>) {
    ui.horizontal(|ui| {
        ui.vertical(|ui| {
            section_label(ui, "SCENE WORKSPACE");
            ui.heading(scene.map(|s| s.name.as_str()).unwrap_or("No scene loaded"));
        });
        ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
            if let Some(scene) = scene {
                ui.label(RichText::new(format!("{} ENTITIES", scene.entities.len())).size(10.0).strong().color(muted()));
            }
        });
    });
    ui.add_space(10.0);

    let available = ui.available_size();
    let size = Vec2::new(available.x.max(280.0), available.y.max(240.0));
    let (rect, _) = ui.allocate_exact_size(size, Sense::hover());
    let painter = ui.painter_at(rect);
    painter.rect_filled(rect, egui::CornerRadius::same(14), Color32::from_rgba_unmultiplied(5, 10, 18, 190));

    let grid = Color32::from_rgba_unmultiplied(90, 125, 176, 28);
    let spacing = 36.0;
    let mut x = rect.left();
    while x <= rect.right() {
        painter.line_segment([egui::pos2(x, rect.top()), egui::pos2(x, rect.bottom())], Stroke::new(1.0, grid));
        x += spacing;
    }
    let mut y = rect.top();
    while y <= rect.bottom() {
        painter.line_segment([egui::pos2(rect.left(), y), egui::pos2(rect.right(), y)], Stroke::new(1.0, grid));
        y += spacing;
    }

    let center = rect.center();
    painter.circle_filled(center, 3.5, accent());
    painter.circle_stroke(center, 24.0, Stroke::new(1.0, Color32::from_rgba_unmultiplied(105, 155, 255, 80)));
    painter.circle_stroke(center, 48.0, Stroke::new(1.0, Color32::from_rgba_unmultiplied(105, 155, 255, 35)));

    painter.text(
        rect.left_top() + Vec2::new(18.0, 18.0),
        egui::Align2::LEFT_TOP,
        "VIEWPORT  /  EDITOR SURFACE",
        egui::FontId::proportional(10.0),
        muted(),
    );
    painter.text(
        rect.left_bottom() + Vec2::new(18.0, -18.0),
        egui::Align2::LEFT_BOTTOM,
        "SCENE DATA PREVIEW  ·  RENDERER SURFACE INTEGRATION IS A SEPARATE MILESTONE",
        egui::FontId::proportional(9.0),
        muted(),
    );
}

fn assets_panel(ui: &mut egui::Ui, assets: &[AssetRecord], selected: &mut Option<PathBuf>) {
    section_label(ui, "GENERATED CONTENT");
    ui.heading("Asset library");
    ui.label(RichText::new("Models generated by the VantaEngine Python authoring SDK.")
        .size(11.0).color(muted()));
    ui.add_space(12.0);

    if assets.is_empty() {
        ui.group(|ui| {
            ui.heading("No generated assets yet");
            ui.label(RichText::new("Run Build Assets from the toolbar to populate this library.")
                .size(11.0).color(muted()));
        });
        return;
    }

    egui::ScrollArea::vertical().show(ui, |ui| {
        for asset in assets {
            let is_selected = selected.as_ref() == Some(&asset.metadata_path);
            egui::Frame::new()
                .fill(if is_selected {
                    Color32::from_rgba_unmultiplied(44, 72, 116, 170)
                } else {
                    Color32::from_rgba_unmultiplied(24, 34, 50, 170)
                })
                .stroke(Stroke::new(1.0, Color32::from_rgba_unmultiplied(126, 158, 210, 42)))
                .corner_radius(egui::CornerRadius::same(12))
                .inner_margin(egui::Margin::same(12))
                .show(ui, |ui| {
                    ui.horizontal(|ui| {
                        ui.vertical(|ui| {
                            ui.label(RichText::new(&asset.metadata.model).strong().size(14.0));
                            ui.label(RichText::new(asset.metadata.files.keys().cloned().collect::<Vec<_>>().join("  ·  "))
                                .size(10.0).color(muted()));
                        });
                        ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                            if ui.button(if is_selected { "Selected" } else { "Inspect" }).clicked() {
                                *selected = Some(asset.metadata_path.clone());
                            }
                        });
                    });
                    ui.add_space(8.0);
                    ui.horizontal(|ui| {
                        metric(ui, "MESHES", asset.metadata.geometry.mesh_count.to_string());
                        metric(ui, "VERTICES", asset.metadata.geometry.vertex_count.to_string());
                        metric(ui, "TRIANGLES", asset.metadata.geometry.triangle_count.to_string());
                        metric(ui, "MATERIALS", asset.metadata.geometry.material_count.to_string());
                    });
                });
            ui.add_space(8.0);
        }
    });
}

fn build_panel(ui: &mut egui::Ui, build_log: &str) {
    section_label(ui, "PIPELINE");
    ui.heading("Build output");
    ui.label(RichText::new("The same deterministic SDK build command used by CI.")
        .size(11.0).color(muted()));
    ui.add_space(12.0);
    egui::Frame::new()
        .fill(Color32::from_rgba_unmultiplied(5, 9, 15, 220))
        .stroke(Stroke::new(1.0, Color32::from_rgba_unmultiplied(115, 147, 190, 35)))
        .corner_radius(egui::CornerRadius::same(12))
        .inner_margin(egui::Margin::same(14))
        .show(ui, |ui| {
            egui::ScrollArea::vertical().stick_to_bottom(true).show(ui, |ui| {
                ui.monospace(build_log);
            });
        });
}

fn transform_group(
    ui: &mut egui::Ui,
    label: &str,
    values: &mut [f32; 3],
    speed: f64,
    dirty: &mut bool,
) {
    ui.label(RichText::new(label).size(9.0).strong().color(muted()));
    ui.horizontal(|ui| {
        for (axis, name) in ["X", "Y", "Z"].into_iter().enumerate() {
            ui.vertical(|ui| {
                ui.label(RichText::new(name).size(9.0).color(muted()));
                *dirty |= ui.add(egui::DragValue::new(&mut values[axis]).speed(speed).max_decimals(3)).changed();
            });
        }
    });
    ui.add_space(6.0);
}

fn metric(ui: &mut egui::Ui, label: &str, value: String) {
    ui.vertical(|ui| {
        ui.label(RichText::new(label).size(8.0).strong().color(muted()));
        ui.label(RichText::new(value).size(12.0).strong());
    });
}

fn section_label(ui: &mut egui::Ui, label: &str) {
    ui.label(RichText::new(label).size(9.0).strong().color(accent()));
}

fn configure_theme(ctx: &egui::Context) {
    let mut style = (*ctx.style()).clone();
    style.spacing.item_spacing = Vec2::new(10.0, 8.0);
    style.spacing.button_padding = Vec2::new(12.0, 8.0);
    style.visuals = egui::Visuals::dark();
    style.visuals.panel_fill = Color32::from_rgba_unmultiplied(10, 15, 25, 190);
    style.visuals.window_fill = Color32::from_rgba_unmultiplied(17, 24, 37, 224);
    style.visuals.extreme_bg_color = Color32::from_rgb(5, 9, 15);
    style.visuals.faint_bg_color = Color32::from_rgba_unmultiplied(60, 82, 116, 34);
    style.visuals.selection.bg_fill = Color32::from_rgba_unmultiplied(68, 118, 206, 120);
    style.visuals.widgets.noninteractive.bg_fill = Color32::from_rgba_unmultiplied(25, 35, 51, 160);
    style.visuals.widgets.inactive.bg_fill = Color32::from_rgba_unmultiplied(34, 46, 66, 190);
    style.visuals.widgets.hovered.bg_fill = Color32::from_rgba_unmultiplied(58, 84, 126, 220);
    style.visuals.widgets.active.bg_fill = Color32::from_rgba_unmultiplied(55, 105, 194, 230);
    ctx.set_style(style);
}

fn glass_frame(fill: Color32, radius: f32) -> egui::Frame {
    egui::Frame::new()
        .fill(fill)
        .stroke(Stroke::new(1.0, Color32::from_rgba_unmultiplied(143, 174, 224, 40)))
        .corner_radius(egui::CornerRadius::same(radius as u8))
        .inner_margin(egui::Margin::same(12))
}

fn primary_button(label: &str) -> egui::Button<'_> {
    egui::Button::new(RichText::new(label).size(10.0).strong().color(Color32::WHITE))
        .fill(Color32::from_rgb(54, 105, 205))
        .corner_radius(egui::CornerRadius::same(8))
}

fn secondary_button(label: &str) -> egui::Button<'_> {
    egui::Button::new(RichText::new(label).size(10.0).strong())
        .fill(Color32::from_rgba_unmultiplied(44, 59, 82, 190))
        .stroke(Stroke::new(1.0, Color32::from_rgba_unmultiplied(143, 174, 224, 48)))
        .corner_radius(egui::CornerRadius::same(8))
}

fn accent() -> Color32 {
    Color32::from_rgb(119, 164, 255)
}

fn muted() -> Color32 {
    Color32::from_rgb(142, 157, 181)
}

fn repository_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
        .canonicalize().unwrap_or_else(|_| PathBuf::from("../.."))
}

fn open_in_system_editor(path: &Path) {
    #[cfg(target_os = "windows")]
    let result = Command::new("explorer").arg(path).spawn();
    #[cfg(target_os = "macos")]
    let result = Command::new("open").arg(path).spawn();
    #[cfg(all(unix, not(target_os = "macos")))]
    let result = Command::new("xdg-open").arg(path).spawn();
    if let Err(error) = result {
        eprintln!("Could not open {}: {error}", path.display());
    }
}
