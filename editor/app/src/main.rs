mod assets;
mod build;
mod project;
mod scene;
mod settings;

use assets::{AssetRecord, discover_assets, project_files};
use build::{BuildResult, build_project};
use iced::widget::{button, column, container, horizontal_space, row, scrollable, text, text_input, vertical_space};
use iced::{Alignment, Background, Border, Color, Element, Length, Subscription, Task, Theme};
use project::{Project, create_project};
use scene::{Scene, SceneEntity, scene_path};
use settings::EditorSettings;
use std::path::{Path, PathBuf};
use std::sync::mpsc::{self, Receiver};
use std::thread;
use std::time::Duration;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Section { Workspace, Scene, Assets, Build, Settings }

#[derive(Debug, Clone, Copy)]
enum TransformField { Position, Rotation, Scale }

#[derive(Debug, Clone)]
enum Message {
    NewProject,
    OpenProject,
    OpenRecent(PathBuf),
    Navigate(Section),
    BuildAssets,
    PollBuild,
    Refresh,
    SelectAsset(PathBuf),
    SelectEntity(u64),
    AddEntity(&'static str),
    DuplicateEntity,
    DeleteEntity,
    EntityNameChanged(String),
    RenameEntity,
    AdjustTransform(TransformField, usize, f32),
    SaveScene,
    OpenFile(PathBuf),
    SetAccent(&'static str),
    ToggleCompact,
    DismissError,
}

struct EditorApp {
    project: Option<Project>,
    section: Section,
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
}

impl Default for EditorApp {
    fn default() -> Self {
        Self {
            project: None,
            section: Section::Workspace,
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
            build_log: "Create a project or open an existing VantaEngine project.".into(),
            error: None,
        }
    }
}

impl EditorApp {
    fn update(&mut self, message: Message) -> Task<Message> {
        match message {
            Message::NewProject => {
                if let Some(directory) = rfd::FileDialog::new()
                    .set_title("Choose an empty folder for the new project").pick_folder()
                {
                    match create_project(&directory) {
                        Ok(project) => self.set_project(project),
                        Err(error) => self.error = Some(error),
                    }
                }
            }
            Message::OpenProject => {
                if let Some(directory) = rfd::FileDialog::new()
                    .set_title("Open VantaEngine project").pick_folder()
                {
                    match Project::open(directory) {
                        Ok(project) => self.set_project(project),
                        Err(error) => self.error = Some(error),
                    }
                }
            }
            Message::OpenRecent(path) => match Project::open(path) {
                Ok(project) => self.set_project(project),
                Err(error) => self.error = Some(error),
            },
            Message::Navigate(section) => self.section = section,
            Message::BuildAssets => self.start_build(),
            Message::PollBuild => self.poll_build(),
            Message::Refresh => self.refresh_project_data(),
            Message::SelectAsset(path) => self.selected_asset = Some(path),
            Message::SelectEntity(id) => {
                self.selected_entity = Some(id);
                self.entity_name_draft = self.scene.as_ref()
                    .and_then(|scene| scene.entities.iter().find(|entity| entity.id == id))
                    .map(|entity| entity.name.clone()).unwrap_or_default();
            }
            Message::AddEntity(kind) => {
                if let Some(scene) = &mut self.scene {
                    let label = match kind { "Camera" => "Camera", "PointLight" => "Point Light", _ => "Cube" };
                    let id = scene.add_entity(label, kind);
                    self.selected_entity = Some(id);
                    self.entity_name_draft = label.into();
                    self.persist_scene();
                }
            }
            Message::DuplicateEntity => {
                if let (Some(scene), Some(selected)) = (&mut self.scene, self.selected_entity) {
                    self.selected_entity = scene.duplicate_entity(selected);
                    self.entity_name_draft = self.selected_entity.and_then(|id|
                        scene.entities.iter().find(|entity| entity.id == id))
                        .map(|entity| entity.name.clone()).unwrap_or_default();
                    self.persist_scene();
                }
            }
            Message::DeleteEntity => {
                if let (Some(scene), Some(selected)) = (&mut self.scene, self.selected_entity) {
                    scene.remove_entity(selected);
                    self.selected_entity = None;
                    self.entity_name_draft.clear();
                    self.persist_scene();
                }
            }
            Message::EntityNameChanged(value) => self.entity_name_draft = value,
            Message::RenameEntity => {
                if let (Some(scene), Some(selected)) = (&mut self.scene, self.selected_entity) {
                    if !self.entity_name_draft.trim().is_empty() {
                        if let Some(entity) = scene.entities.iter_mut().find(|entity| entity.id == selected) {
                            entity.name = self.entity_name_draft.trim().to_owned();
                            self.persist_scene();
                        }
                    }
                }
            }
            Message::AdjustTransform(field, axis, delta) => {
                if let (Some(scene), Some(selected)) = (&mut self.scene, self.selected_entity) {
                    if let Some(entity) = scene.entities.iter_mut().find(|entity| entity.id == selected) {
                        let values = match field {
                            TransformField::Position => &mut entity.transform.position,
                            TransformField::Rotation => &mut entity.transform.rotation,
                            TransformField::Scale => &mut entity.transform.scale,
                        };
                        if axis < 3 {
                            values[axis] += delta;
                            if matches!(field, TransformField::Scale) {
                                values[axis] = values[axis].max(0.01);
                            }
                        }
                        self.persist_scene();
                    }
                }
            }
            Message::SaveScene => self.persist_scene(),
            Message::OpenFile(path) => open_in_system_editor(&path),
            Message::SetAccent(accent) => {
                self.settings.accent = accent.into();
                self.settings.save();
            }
            Message::ToggleCompact => {
                self.settings.compact_mode = !self.settings.compact_mode;
                self.settings.save();
            }
            Message::DismissError => self.error = None,
        }
        Task::none()
    }

    fn view(&self) -> Element<'_, Message> {
        let header = row![
            column![
                text("VANTAENGINE").size(11).color(self.accent_color()),
                text("Editor").size(25),
            ].spacing(1),
            horizontal_space(),
            button("New project").on_press(Message::NewProject),
            button("Open project").on_press(Message::OpenProject),
            button("Save scene").on_press_maybe(self.scene.is_some().then_some(Message::SaveScene)),
            button(if self.build_running { "Building…" } else { "Build assets" })
                .style(button::primary)
                .on_press_maybe((self.project.is_some() && !self.build_running).then_some(Message::BuildAssets)),
        ].spacing(9).align_y(Alignment::Center);

        let sidebar = self.sidebar();
        let content = self.content();
        let status = row![
            text(if self.build_running { "● BUILD RUNNING" } else { "● READY" }).size(10)
                .color(if self.build_running { Color::from_rgb8(255, 190, 90) } else { Color::from_rgb8(95, 202, 151) }),
            text(&self.scene_status).size(10).color(Color::from_rgb8(135, 148, 166)),
            horizontal_space(),
            text(self.project.as_ref().map(|project| project.root.display().to_string())
                .unwrap_or_else(|| "No project open".into()))
                .size(10).color(Color::from_rgb8(135, 148, 166)),
        ].spacing(18).align_y(Alignment::Center);

        let mut layout = column![
            container(header).padding([15, 20]).width(Length::Fill),
            row![sidebar, content].height(Length::Fill).spacing(0),
            container(status).padding([9, 20]).width(Length::Fill),
        ].spacing(0).width(Length::Fill).height(Length::Fill);

        if let Some(error) = &self.error {
            layout = layout.push(container(row![
                text(error).size(12),
                horizontal_space(),
                button("Dismiss").on_press(Message::DismissError),
            ].align_y(Alignment::Center)).padding(10).width(Length::Fill));
        }
        container(layout).width(Length::Fill).height(Length::Fill).into()
    }

    fn sidebar(&self) -> Element<'_, Message> {
        let project_name = self.project.as_ref().map(|project| project.name.as_str()).unwrap_or("NO PROJECT");
        let mut sidebar = column![
            text("PROJECT").size(10).color(Color::from_rgb8(126, 143, 164)),
            text(project_name).size(15),
            vertical_space().height(12),
            nav_button("Overview", Section::Workspace, self.section == Section::Workspace),
            nav_button("Scene", Section::Scene, self.section == Section::Scene),
            nav_button("Asset library", Section::Assets, self.section == Section::Assets),
            nav_button("Build output", Section::Build, self.section == Section::Build),
            nav_button("Settings", Section::Settings, self.section == Section::Settings),
            vertical_space().height(14),
            text("PROJECT FILES").size(10).color(Color::from_rgb8(126, 143, 164)),
        ].spacing(if self.settings.compact_mode { 4 } else { 7 });

        if self.project.is_none() {
            sidebar = sidebar.push(text("Open a project to browse files.").size(11)
                .color(Color::from_rgb8(135, 148, 166)));
        } else {
            for (path, is_directory) in self.files.iter().take(90) {
                let label = format!("{} {}", if *is_directory { "▸" } else { "·" }, path.display());
                if *is_directory {
                    sidebar = sidebar.push(text(label).size(10).color(Color::from_rgb8(145, 158, 176)));
                } else if let Some(project) = &self.project {
                    sidebar = sidebar.push(button(text(label).size(10)).width(Length::Fill)
                        .padding([3, 5]).on_press(Message::OpenFile(project.root.join(path))));
                }
            }
        }
        container(scrollable(sidebar)).width(Length::Fixed(238.0)).height(Length::Fill).padding(14)
            .style(|_| panel_style(Color::from_rgb8(19, 24, 32))).into()
    }

    fn content(&self) -> Element<'_, Message> {
        let body = match self.section {
            Section::Workspace => self.workspace_view(),
            Section::Scene => self.scene_view(),
            Section::Assets => self.assets_view(),
            Section::Build => self.build_view(),
            Section::Settings => self.settings_view(),
        };
        container(body).width(Length::Fill).height(Length::Fill).padding([20, 24]).into()
    }

    fn workspace_view(&self) -> Element<'_, Message> {
        let Some(project) = &self.project else {
            let mut recent = column![text("RECENT PROJECTS").size(10)
                .color(Color::from_rgb8(126, 143, 164))].spacing(8);
            if self.settings.recent_projects.is_empty() {
                recent = recent.push(text("Projects you open will appear here.").size(12)
                    .color(Color::from_rgb8(135, 148, 166)));
            } else {
                for path in self.settings.recent_projects.iter().take(8) {
                    let path = PathBuf::from(path);
                    let label = path.file_name().and_then(|name| name.to_str()).unwrap_or("Project").to_owned();
                    recent = recent.push(button(row![
                        text(label).size(13),
                        horizontal_space(),
                        text(path.display().to_string()).size(10).color(Color::from_rgb8(135, 148, 166)),
                    ]).width(Length::Fill).padding(10).on_press(Message::OpenRecent(path)));
                }
            }
            return container(column![
                text("VantaEngine").size(34),
                text("A native workspace for building scenes and procedural assets.")
                    .size(14).color(Color::from_rgb8(151, 164, 183)),
                row![
                    button("Create project").style(button::primary).on_press(Message::NewProject),
                    button("Open existing project").on_press(Message::OpenProject),
                ].spacing(10),
                container(recent).padding(18).width(Length::Fill)
                    .style(|_| panel_style(Color::from_rgb8(20, 26, 35))),
            ].spacing(18)).width(Length::Fill).center_y(Length::Fill).into();
        };

        let stats = row![
            stat_card("SCENE OBJECTS", self.scene.as_ref().map(|scene| scene.entities.len()).unwrap_or(0).to_string()),
            stat_card("GENERATED ASSETS", self.assets.len().to_string()),
            stat_card("ENTRY SCRIPT", project.entrypoint.file_name().and_then(|value| value.to_str())
                .unwrap_or("generate.py").to_owned()),
        ].spacing(12);

        container(column![
            text(format!("Welcome back, {}", project.name)).size(27),
            text(project.root.display().to_string()).size(11).color(Color::from_rgb8(135, 148, 166)),
            stats,
            text("WORKSPACE").size(10).color(Color::from_rgb8(126, 143, 164)),
            row![
                action_card("Scene editor", "Create and organize scene entities.", Message::Navigate(Section::Scene)),
                action_card("Asset library", "Inspect generated models and exports.", Message::Navigate(Section::Assets)),
                action_card("Build pipeline", "Run the Python authoring SDK.", Message::Navigate(Section::Build)),
            ].spacing(10),
            text("LATEST BUILD OUTPUT").size(10).color(Color::from_rgb8(126, 143, 164)),
            container(scrollable(text(self.build_log.clone()).size(11)).height(Length::Fixed(110.0)))
                .padding(14).width(Length::Fill).style(|_| panel_style(Color::from_rgb8(14, 19, 26))),
        ].spacing(16)).width(Length::Fill).into()
    }

    fn scene_view(&self) -> Element<'_, Message> {
        let Some(scene) = &self.scene else {
            return empty_state("No project scene is open", "Create or open a project to edit its scene.");
        };
        let toolbar = row![
            button("＋ Cube").on_press(Message::AddEntity("StaticMesh")),
            button("＋ Camera").on_press(Message::AddEntity("Camera")),
            button("＋ Light").on_press(Message::AddEntity("PointLight")),
            horizontal_space(),
            button("Duplicate").on_press_maybe(self.selected_entity.map(|_| Message::DuplicateEntity)),
            button("Delete").on_press_maybe(self.selected_entity.map(|_| Message::DeleteEntity)),
            button("Save").style(button::primary).on_press(Message::SaveScene),
        ].spacing(7).align_y(Alignment::Center);

        let mut hierarchy = column![
            text("HIERARCHY").size(10).color(Color::from_rgb8(126, 143, 164)),
            text(&scene.name).size(15),
        ].spacing(8);
        if scene.entities.is_empty() {
            hierarchy = hierarchy.push(text("This scene is empty. Add an object to begin.")
                .size(11).color(Color::from_rgb8(135, 148, 166)));
        } else {
            for entity in &scene.entities {
                let selected = self.selected_entity == Some(entity.id);
                let item = button(row![
                    text(if entity.kind == "Camera" { "◉" } else if entity.kind == "PointLight" { "☼" } else { "◇" })
                        .color(self.accent_color()),
                    text(&entity.name).size(12),
                    horizontal_space(),
                    text(&entity.kind).size(9).color(Color::from_rgb8(126, 143, 164)),
                ].align_y(Alignment::Center)).width(Length::Fill).padding([8, 9]);
                hierarchy = if selected {
                    hierarchy.push(item.style(button::primary).on_press(Message::SelectEntity(entity.id)))
                } else {
                    hierarchy.push(item.style(button::secondary).on_press(Message::SelectEntity(entity.id)))
                };
            }
        }

        let inspector: Element<'_, Message> = if let Some(entity) = self.selected_entity
            .and_then(|id| scene.entities.iter().find(|entity| entity.id == id)) {
            self.entity_inspector(entity)
        } else {
            container(column![
                text("INSPECTOR").size(10).color(Color::from_rgb8(126, 143, 164)),
                text("Nothing selected").size(16),
                text("Select an entity in the hierarchy to inspect its properties.")
                    .size(11).color(Color::from_rgb8(135, 148, 166)),
            ].spacing(10)).padding(14).width(Length::Fill).into()
        };

        container(column![
            text("Scene editor").size(26),
            text("Scene data is saved as a versioned, engine-independent project asset.")
                .size(11).color(Color::from_rgb8(135, 148, 166)),
            toolbar,
            row![
                container(scrollable(hierarchy)).width(Length::FillPortion(4)).height(Length::Fill)
                    .padding(12).style(|_| panel_style(Color::from_rgb8(19, 24, 32))),
                container(column![
                    text("VIEWPORT").size(10).color(Color::from_rgb8(126, 143, 164)),
                    vertical_space().height(Length::Fill),
                    text("Scene data preview").size(18),
                    text(format!("{} entities · {} selected", scene.entities.len(),
                        if self.selected_entity.is_some() { 1 } else { 0 }))
                        .size(11).color(Color::from_rgb8(135, 148, 166)),
                    text("The renderer does not expose an embeddable editor surface yet.")
                        .size(10).color(Color::from_rgb8(135, 148, 166)),
                    vertical_space().height(Length::Fill),
                ].spacing(10).align_x(Alignment::Center))
                    .width(Length::FillPortion(9)).height(Length::Fill).padding(16)
                    .style(|_| panel_style(Color::from_rgb8(13, 18, 25))),
                container(inspector).width(Length::FillPortion(4)).height(Length::Fill)
                    .padding(12).style(|_| panel_style(Color::from_rgb8(19, 24, 32))),
            ].spacing(10).height(Length::Fill),
        ].spacing(12)).width(Length::Fill).height(Length::Fill).into()
    }

    fn entity_inspector(&self, entity: &SceneEntity) -> Element<'_, Message> {
        let mut panel = column![
            text("INSPECTOR").size(10).color(Color::from_rgb8(126, 143, 164)),
            text(&entity.kind).size(10).color(self.accent_color()),
            text_input("Entity name", &self.entity_name_draft).on_input(Message::EntityNameChanged),
            button("Apply name").on_press(Message::RenameEntity),
            text("TRANSFORM").size(10).color(Color::from_rgb8(126, 143, 164)),
            vector_editor("Position", &entity.transform.position, TransformField::Position),
            vector_editor("Rotation", &entity.transform.rotation, TransformField::Rotation),
            vector_editor("Scale", &entity.transform.scale, TransformField::Scale),
        ].spacing(10);
        if entity.kind == "Camera" {
            panel = panel.push(text("Camera component").size(12))
                .push(text("Camera settings are stored with this entity type; runtime camera integration is a separate engine contract.")
                    .size(10).color(Color::from_rgb8(135, 148, 166)));
        } else if entity.kind == "PointLight" {
            panel = panel.push(text("Point light component").size(12))
                .push(text("Light authoring properties will be connected to the renderer's public scene schema.")
                    .size(10).color(Color::from_rgb8(135, 148, 166)));
        }
        panel.into()
    }

    fn assets_view(&self) -> Element<'_, Message> {
        let mut items = column![
            text("Asset library").size(26),
            text("Generated models, geometry statistics and exported files.")
                .size(11).color(Color::from_rgb8(145, 158, 176)),
        ].spacing(12);
        if self.assets.is_empty() {
            items = items.push(container(column![
                text("No generated assets yet").size(17),
                text("Run the asset pipeline to populate this library.").size(11)
                    .color(Color::from_rgb8(145, 158, 176)),
                button("Build assets").style(button::primary)
                    .on_press_maybe((self.project.is_some() && !self.build_running).then_some(Message::BuildAssets)),
            ].spacing(9)).padding(20).width(Length::Fill)
                .style(|_| panel_style(Color::from_rgb8(20, 26, 35))));
        } else {
            for asset in &self.assets {
                items = items.push(asset_card(asset, self.selected_asset.as_ref()));
                if self.selected_asset.as_ref() == Some(&asset.metadata_path) {
                    let files = asset.metadata.files.iter().map(|(kind, path)| format!("{kind}: {path}"))
                        .collect::<Vec<_>>().join("\n");
                    items = items.push(container(column![
                        text("ASSET INSPECTOR").size(10).color(Color::from_rgb8(126, 143, 164)),
                        text(format!("Model: {}", asset.metadata.model)).size(13),
                        text(format!("Schema: {}  ·  Materials: {}", asset.metadata.schema_version,
                            asset.metadata.geometry.material_count)).size(10),
                        text(format!("Meshes: {}  ·  Vertices: {}  ·  Triangles: {}",
                            asset.metadata.geometry.mesh_count, asset.metadata.geometry.vertex_count,
                            asset.metadata.geometry.triangle_count)).size(10),
                        text(if files.is_empty() { "No exported files recorded.".into() } else { files }).size(10),
                        text(asset.metadata_path.display().to_string()).size(9)
                            .color(Color::from_rgb8(126, 143, 164)),
                    ].spacing(7)).padding(14).width(Length::Fill)
                        .style(|_| panel_style(Color::from_rgb8(15, 22, 32))));
                }
            }
        }
        container(scrollable(items)).width(Length::Fill).height(Length::Fill).into()
    }

    fn build_view(&self) -> Element<'_, Message> {
        container(column![
            text("Build pipeline").size(26),
            text("Run the same Python SDK command used by the repository's asset pipeline.")
                .size(11).color(Color::from_rgb8(145, 158, 176)),
            row![
                button(if self.build_running { "Build running…" } else { "Build project assets" })
                    .style(button::primary)
                    .on_press_maybe((self.project.is_some() && !self.build_running).then_some(Message::BuildAssets)),
                button("Refresh library").on_press(Message::Refresh),
            ].spacing(8),
            container(scrollable(text(self.build_log.clone()).size(11)).height(Length::Fill))
                .padding(15).width(Length::Fill).height(Length::Fill)
                .style(|_| panel_style(Color::from_rgb8(13, 18, 25))),
        ].spacing(14)).width(Length::Fill).height(Length::Fill).into()
    }

    fn settings_view(&self) -> Element<'_, Message> {
        container(column![
            text("Editor settings").size(26),
            text("Workspace preferences are stored locally on this machine.")
                .size(11).color(Color::from_rgb8(145, 158, 176)),
            text("ACCENT COLOR").size(10).color(Color::from_rgb8(126, 143, 164)),
            row![
                choice_button("Vanta blue", self.settings.accent == "blue", Message::SetAccent("blue")),
                choice_button("Mint", self.settings.accent == "mint", Message::SetAccent("mint")),
                choice_button("Violet", self.settings.accent == "violet", Message::SetAccent("violet")),
            ].spacing(8),
            text("LAYOUT").size(10).color(Color::from_rgb8(126, 143, 164)),
            button(if self.settings.compact_mode { "Use comfortable spacing" } else { "Use compact spacing" })
                .on_press(Message::ToggleCompact),
            text("PROJECT HISTORY").size(10).color(Color::from_rgb8(126, 143, 164)),
            text(format!("{} recent projects are retained locally.", self.settings.recent_projects.len()))
                .size(12),
            text("No telemetry or account connection is used by the editor.")
                .size(11).color(Color::from_rgb8(135, 148, 166)),
        ].spacing(14)).width(Length::Fill).into()
    }

    fn set_project(&mut self, project: Project) {
        self.settings.remember_project(&project.root);
        self.project = Some(project);
        self.section = Section::Workspace;
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
                self.section = Section::Build;
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
                    self.section = Section::Assets;
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

    fn subscription(&self) -> Subscription<Message> {
        if self.build_running {
            iced::time::every(Duration::from_millis(120)).map(|_| Message::PollBuild)
        } else { Subscription::none() }
    }

    fn accent_color(&self) -> Color {
        match self.settings.accent.as_str() {
            "mint" => Color::from_rgb8(91, 207, 163),
            "violet" => Color::from_rgb8(170, 132, 255),
            _ => Color::from_rgb8(111, 155, 255),
        }
    }

    fn theme(&self) -> Theme {
        Theme::custom("Vanta".into(), iced::theme::Palette {
            background: Color::from_rgb8(13, 17, 23),
            text: Color::from_rgb8(232, 237, 245),
            primary: self.accent_color(),
            success: Color::from_rgb8(75, 196, 145),
            danger: Color::from_rgb8(242, 100, 112),
        })
    }
}

fn choice_button(label: &'static str, selected: bool, message: Message) -> iced::widget::Button<'static, Message> {
    let control = button(label);
    if selected { control.style(button::primary).on_press(message) }
    else { control.style(button::secondary).on_press(message) }
}

fn nav_button(label: &'static str, section: Section, selected: bool) -> iced::widget::Button<'static, Message> {
    let control = button(text(label).size(13)).width(Length::Fill).padding([9, 11]);
    if selected { control.style(button::primary).on_press(Message::Navigate(section)) }
    else { control.style(button::text).on_press(Message::Navigate(section)) }
}

fn stat_card(label: &'static str, value: String) -> Element<'static, Message> {
    container(column![
        text(label).size(9).color(Color::from_rgb8(126, 143, 164)),
        text(value).size(16),
    ].spacing(7)).padding(14).width(Length::Fill)
        .style(|_| panel_style(Color::from_rgb8(20, 26, 35))).into()
}

fn action_card(label: &'static str, description: &'static str, message: Message) -> Element<'static, Message> {
    container(button(column![
        text(label).size(14),
        text(description).size(10).color(Color::from_rgb8(135, 148, 166)),
    ].spacing(6)).width(Length::Fill).padding(12).on_press(message))
        .width(Length::Fill).style(|_| panel_style(Color::from_rgb8(20, 26, 35))).into()
}

fn vector_editor(label: &'static str, values: &[f32; 3], field: TransformField) -> Element<'static, Message> {
    let axis = |name: &'static str, index: usize| {
        row![
            text(name).size(10).width(Length::Fixed(13.0)),
            button("−").padding([3, 8]).on_press(Message::AdjustTransform(field, index, -0.1)),
            text(format!("{:.2}", values[index])).size(10).width(Length::Fixed(43.0)),
            button("+").padding([3, 8]).on_press(Message::AdjustTransform(field, index, 0.1)),
        ].spacing(4).align_y(Alignment::Center)
    };
    container(column![
        text(label).size(11),
        row![axis("X", 0), axis("Y", 1), axis("Z", 2)].spacing(5),
    ].spacing(6)).into()
}

fn asset_card<'a>(asset: &'a AssetRecord, selected: Option<&PathBuf>) -> Element<'a, Message> {
    let stats = &asset.metadata.geometry;
    let is_selected = selected.is_some_and(|path| path == &asset.metadata_path);
    let content = row![
        column![
            text(&asset.metadata.model).size(15),
            text(asset.metadata.files.keys().cloned().collect::<Vec<_>>().join("  ·  "))
                .size(10).color(Color::from_rgb8(135, 148, 166)),
        ].spacing(4).width(Length::Fill),
        text(format!("{} meshes", stats.mesh_count)).size(10),
        text(format!("{} tris", stats.triangle_count)).size(10),
        text(format!("{} verts", stats.vertex_count)).size(10),
        button(if is_selected { "Selected" } else { "Inspect" })
            .on_press(Message::SelectAsset(asset.metadata_path.clone())),
    ].spacing(12).align_y(Alignment::Center);
    container(content).padding(13).width(Length::Fill)
        .style(move |_| panel_style(if is_selected {
            Color::from_rgb8(32, 48, 75)
        } else { Color::from_rgb8(20, 26, 35) })).into()
}

fn empty_state(title: &'static str, description: &'static str) -> Element<'static, Message> {
    container(column![
        text(title).size(20),
        text(description).size(12).color(Color::from_rgb8(145, 158, 176)),
    ].spacing(9)).center_x(Length::Fill).center_y(Length::Fill).into()
}

fn panel_style(color: Color) -> container::Style {
    container::Style {
        background: Some(Background::Color(color)),
        border: Border { color: Color::from_rgb8(42, 52, 67), width: 1.0, radius: 8.0.into() },
        ..container::Style::default()
    }
}

fn repository_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
        .canonicalize().unwrap_or_else(|_| PathBuf::from("../.."))
}

fn open_in_system_editor(path: &Path) {
    #[cfg(target_os = "windows")]
    let result = std::process::Command::new("explorer").arg(path).spawn();
    #[cfg(target_os = "macos")]
    let result = std::process::Command::new("open").arg(path).spawn();
    #[cfg(all(unix, not(target_os = "macos")))]
    let result = std::process::Command::new("xdg-open").arg(path).spawn();
    if let Err(error) = result {
        eprintln!("Could not open {}: {error}", path.display());
    }
}

fn main() -> iced::Result {
    iced::application("VantaEngine Editor", EditorApp::update, EditorApp::view)
        .subscription(EditorApp::subscription)
        .theme(EditorApp::theme)
        .window_size(iced::Size::new(1500.0, 940.0))
        .run()
}
