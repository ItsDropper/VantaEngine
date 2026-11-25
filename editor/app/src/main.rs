mod assets;
mod build;
mod egui_app;
mod project;
mod scene;
mod settings;

fn main() -> eframe::Result {
    let viewport = eframe::egui::ViewportBuilder::default()
        .with_inner_size([1560.0, 980.0])
        .with_min_inner_size([1120.0, 720.0])
        .with_transparent(true);

    let options = eframe::NativeOptions {
        viewport,
        ..Default::default()
    };

    eframe::run_native(
        "VantaEngine Editor",
        options,
        Box::new(|context| Ok(Box::new(egui_app::EditorApp::new(context)))),
    )
}
