mod assets;
mod build;
mod project;

use assets::{AssetRecord, discover_assets, project_files};
use build::{BuildResult, build_project};
use iced::widget::{button, column, container, horizontal_space, row, scrollable, text, vertical_space};
use iced::{Alignment, Background, Border, Color, Element, Length, Subscription, Task, Theme};
use project::{Project, create_project};
use std::path::{Path, PathBuf};
use std::sync::mpsc::{self, Receiver};
use std::thread;
use std::time::Duration;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Section { Workspace, Assets, Build }

#[derive(Debug, Clone)]
enum Message {
    NewProject,
    OpenProject,
    Navigate(Section),
    BuildAssets,
    PollBuild,
    Refresh,
    SelectAsset(PathBuf),
    OpenFile(PathBuf),
    DismissError,
}

struct EditorApp {
    project: Option<Project>,
    section: Section,
    assets: Vec<AssetRecord>,
    selected_asset: Option<PathBuf>,
    files: Vec<(PathBuf, bool)>,
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
            build_receiver: None,
            build_running: false,
            build_log: "Open or create a project to get started.".into(),
            error: None,
        }
    }
}

impl EditorApp {
    fn update(&mut self, message: Message) -> Task<Message> {
        match message {
            Message::NewProject => {
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
            Message::OpenProject => {
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
            Message::Navigate(section) => self.section = section,
            Message::BuildAssets => self.start_build(),
            Message::PollBuild => self.poll_build(),
            Message::Refresh => self.refresh_project_data(),
            Message::SelectAsset(path) => self.selected_asset = Some(path),
            Message::OpenFile(path) => open_in_system_editor(&path),
            Message::DismissError => self.error = None,
        }
        Task::none()
    }

    fn view(&self) -> Element<'_, Message> {
        let header = row![
            column![
                text("VANTAENGINE").size(12).color(Color::from_rgb8(111, 155, 255)),
                text("Editor").size(25),
            ].spacing(2),
            horizontal_space(),
            button("New project").on_press(Message::NewProject),
            button("Open project").on_press(Message::OpenProject),
            button("Build assets").style(button::primary)
                .on_press_maybe((self.project.is_some() && !self.build_running).then_some(Message::BuildAssets)),
            button("Refresh").on_press(Message::Refresh),
        ].spacing(10).align_y(Alignment::Center);

        let sidebar = self.sidebar();
        let content = self.content();
        let status = row![
            text(if self.build_running { "BUILD RUNNING" } else { "READY" })
                .size(11)
                .color(if self.build_running { Color::from_rgb8(255, 190, 90) } else { Color::from_rgb8(95, 202, 151) }),
            horizontal_space(),
            text(self.project.as_ref().map(|p| p.root.display().to_string())
                .unwrap_or_else(|| "No project open".into()))
                .size(11).color(Color::from_rgb8(135, 148, 166)),
        ].align_y(Alignment::Center);

        let mut layout = column![
            container(header).padding([18, 22]).width(Length::Fill),
            row![sidebar, content].height(Length::Fill).spacing(0),
            container(status).padding([10, 22]).width(Length::Fill),
        ].spacing(0).width(Length::Fill).height(Length::Fill);

        if let Some(error) = &self.error {
            layout = layout.push(container(row![
                text(error).size(13),
                horizontal_space(),
                button("Dismiss").on_press(Message::DismissError),
            ].align_y(Alignment::Center)).padding(12).width(Length::Fill));
        }

        container(layout).width(Length::Fill).height(Length::Fill).into()
    }

    fn sidebar(&self) -> Element<'_, Message> {
        let project_name = self.project.as_ref().map(|p| p.name.as_str()).unwrap_or("NO PROJECT");
        let mut sidebar = column![
            text("PROJECT").size(11).color(Color::from_rgb8(126, 143, 164)),
            text(project_name).size(16),
            vertical_space().height(12),
            nav_button("Overview", Section::Workspace, self.section == Section::Workspace),
            nav_button("Asset library", Section::Assets, self.section == Section::Assets),
            nav_button("Build output", Section::Build, self.section == Section::Build),
            vertical_space().height(16),
            text("FILES").size(11).color(Color::from_rgb8(126, 143, 164)),
        ].spacing(7);

        if self.project.is_none() {
            sidebar = sidebar.push(text("Create or open a project to browse its files.")
                .size(12).color(Color::from_rgb8(135, 148, 166)));
        } else {
            for (path, is_directory) in self.files.iter().take(70) {
                let label = format!("{} {}", if *is_directory { "▸" } else { "·" }, path.display());
                if *is_directory {
                    sidebar = sidebar.push(text(label).size(11).color(Color::from_rgb8(145, 158, 176)));
                } else if let Some(project) = &self.project {
                    sidebar = sidebar.push(button(text(label).size(11))
                        .width(Length::Fill).padding([4, 6])
                        .on_press(Message::OpenFile(project.root.join(path))));
                }
            }
        }

        container(scrollable(sidebar))
            .width(Length::Fixed(250.0)).height(Length::Fill).padding(16)
            .style(|_| panel_style(Color::from_rgb8(20, 26, 35))).into()
    }

    fn content(&self) -> Element<'_, Message> {
        let body = match self.section {
            Section::Workspace => self.workspace_view(),
            Section::Assets => self.assets_view(),
            Section::Build => self.build_view(),
        };
        container(body).width(Length::Fill).height(Length::Fill).padding([22, 26]).into()
    }

    fn workspace_view(&self) -> Element<'_, Message> {
        let Some(project) = &self.project else {
            return container(column![
                text("Build something with VantaEngine.").size(30),
                text("A native GPU-rendered workspace for projects, procedural assets and engine tooling.")
                    .size(15).color(Color::from_rgb8(151, 164, 183)),
                row![
                    button("Create project").style(button::primary).on_press(Message::NewProject),
                    button("Open existing project").on_press(Message::OpenProject),
                ].spacing(10),
            ].spacing(18)).center_x(Length::Fill).center_y(Length::Fill).into();
        };

        let stats = row![
            stat_card("ASSETS", self.assets.len().to_string()),
            stat_card("ENTRYPOINT", project.entrypoint.file_name().and_then(|v| v.to_str())
                .unwrap_or("generate.py").to_owned()),
            stat_card("OUTPUT", project.output.strip_prefix(&project.root)
                .unwrap_or(&project.output).display().to_string()),
        ].spacing(12);

        container(column![
            text(format!("Welcome back, {}", project.name)).size(28),
            text(project.root.display().to_string()).size(12).color(Color::from_rgb8(135, 148, 166)),
            stats,
            text("WORKSPACE ACTIONS").size(11).color(Color::from_rgb8(126, 143, 164)),
            row![
                button("Open authoring script").on_press(Message::OpenFile(project.entrypoint.clone())),
                button("Build assets").style(button::primary)
                    .on_press_maybe((!self.build_running).then_some(Message::BuildAssets)),
                button("Browse assets").on_press(Message::Navigate(Section::Assets)),
            ].spacing(10),
            text("LATEST BUILD OUTPUT").size(11).color(Color::from_rgb8(126, 143, 164)),
            container(text(self.build_log.clone()).size(12)).padding(14).width(Length::Fill)
                .style(|_| panel_style(Color::from_rgb8(15, 20, 27))),
        ].spacing(18)).width(Length::Fill).into()
    }

    fn assets_view(&self) -> Element<'_, Message> {
        let mut items = column![
            text("Asset library").size(27),
            text("Generated models, geometry statistics and exported material files.")
                .size(13).color(Color::from_rgb8(145, 158, 176)),
        ].spacing(12);

        if self.assets.is_empty() {
            items = items.push(container(column![
                text("No generated assets yet").size(18),
                text("Run an asset build to populate this library.")
                    .size(13).color(Color::from_rgb8(145, 158, 176)),
                button("Build assets").style(button::primary)
                    .on_press_maybe((self.project.is_some() && !self.build_running)
                        .then_some(Message::BuildAssets)),
            ].spacing(10)).padding(22).width(Length::Fill)
                .style(|_| panel_style(Color::from_rgb8(20, 26, 35))));
        } else {
            for asset in &self.assets {
                items = items.push(asset_card(asset, self.selected_asset.as_ref()));
            }
        }
        container(scrollable(items)).width(Length::Fill).height(Length::Fill).into()
    }

    fn build_view(&self) -> Element<'_, Message> {
        container(column![
            text("Build").size(27),
            text("Asset generation runs outside the UI thread. The editor invokes the same Python SDK command used by CI.")
                .size(13).color(Color::from_rgb8(145, 158, 176)),
            row![
                button(if self.build_running { "Building…" } else { "Build project assets" })
                    .style(button::primary)
                    .on_press_maybe((self.project.is_some() && !self.build_running)
                        .then_some(Message::BuildAssets)),
                button("Refresh assets").on_press(Message::Refresh),
            ].spacing(10),
            container(scrollable(text(self.build_log.clone()).size(12)).height(Length::Fill))
                .padding(16).width(Length::Fill).height(Length::Fill)
                .style(|_| panel_style(Color::from_rgb8(13, 18, 25))),
        ].spacing(16)).width(Length::Fill).height(Length::Fill).into()
    }

    fn set_project(&mut self, project: Project) {
        self.project = Some(project);
        self.section = Section::Workspace;
        self.selected_asset = None;
        self.refresh_project_data();
        self.build_log = "Project opened. Ready to build assets.".into();
    }

    fn refresh_project_data(&mut self) {
        if let Some(project) = &self.project {
            self.assets = discover_assets(&project.output);
            self.files = project_files(&project.root);
        } else {
            self.assets.clear();
            self.files.clear();
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
            Err(error) => self.error = Some(format!("Could not start asset build worker: {error}")),
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
        } else {
            Subscription::none()
        }
    }

    fn theme(&self) -> Theme {
        Theme::custom("Vanta".into(), iced::theme::Palette {
            background: Color::from_rgb8(13, 17, 23),
            text: Color::from_rgb8(232, 237, 245),
            primary: Color::from_rgb8(78, 133, 255),
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
    let control = button(text(label).size(14)).width(Length::Fill).padding([10, 12]);
    if selected { control.style(button::primary).on_press(Message::Navigate(section)) }
    else { control.on_press(Message::Navigate(section)) }
}

fn stat_card(label: &'static str, value: String) -> Element<'static, Message> {
    container(column![
        text(label).size(10).color(Color::from_rgb8(126, 143, 164)),
        text(value).size(16),
    ].spacing(8)).padding(14).width(Length::Fill)
        .style(|_| panel_style(Color::from_rgb8(20, 26, 35))).into()
}

fn asset_card<'a>(asset: &'a AssetRecord, selected: Option<&PathBuf>) -> Element<'a, Message> {
    let stats = &asset.metadata.geometry;
    let is_selected = selected.is_some_and(|path| path == &asset.metadata_path);
    let content = row![
        column![
            text(&asset.metadata.model).size(17),
            text(asset.metadata.files.keys().cloned().collect::<Vec<_>>().join("  ·  "))
                .size(11).color(Color::from_rgb8(135, 148, 166)),
        ].spacing(5).width(Length::Fill),
        text(format!("{} meshes", stats.mesh_count)).size(12),
        text(format!("{} tris", stats.triangle_count)).size(12),
        text(format!("{} verts", stats.vertex_count)).size(12),
        button("Inspect").on_press(Message::SelectAsset(asset.metadata_path.clone())),
    ].spacing(16).align_y(Alignment::Center);
    container(content).padding(16).width(Length::Fill)
        .style(move |_| panel_style(if is_selected {
            Color::from_rgb8(32, 48, 75)
        } else {
            Color::from_rgb8(20, 26, 35)
        })).into()
}

fn panel_style(color: Color) -> container::Style {
    container::Style {
        background: Some(Background::Color(color)),
        border: Border { color: Color::from_rgb8(42, 52, 67), width: 1.0, radius: 9.0.into() },
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
        .window_size(iced::Size::new(1440.0, 900.0))
        .run()
}
