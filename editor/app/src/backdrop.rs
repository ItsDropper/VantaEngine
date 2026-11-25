//! Native window backdrop integration.
//!
//! Windows 11 uses the compositor-owned Mica material. Other platforms keep
//! the editor's translucent in-app surfaces and use their normal compositor.

#[cfg(windows)]
mod platform {
    use std::thread;
    use std::time::Duration;

    use windows_sys::Win32::Foundation::{BOOL, HWND, LPARAM};
    use windows_sys::Win32::Graphics::Dwm::{
        DwmSetWindowAttribute, DWMWA_SYSTEMBACKDROP_TYPE, DWMWA_USE_IMMERSIVE_DARK_MODE,
        DWMWA_WINDOW_CORNER_PREFERENCE, DWMSBT_MAINWINDOW, DWMWCP_ROUND,
    };
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        EnumWindows, GetWindowTextW, GetWindowThreadProcessId, IsWindowVisible,
    };

    struct WindowSearch {
        process_id: u32,
        handle: Option<HWND>,
    }

    unsafe extern "system" fn find_editor_window(window: HWND, parameter: LPARAM) -> BOOL {
        let search = &mut *(parameter as *mut WindowSearch);
        let mut owner_process = 0_u32;
        GetWindowThreadProcessId(window, &mut owner_process);

        if owner_process != search.process_id || IsWindowVisible(window) == 0 {
            return 1;
        }

        let mut title = [0_u16; 128];
        let length = GetWindowTextW(window, title.as_mut_ptr(), title.len() as i32);
        if length > 0
            && String::from_utf16_lossy(&title[..length as usize]) == "VantaEngine Editor"
        {
            search.handle = Some(window);
            return 0;
        }

        1
    }

    fn apply_mica(window: HWND) {
        unsafe {
            let backdrop = DWMSBT_MAINWINDOW;
            let dark_mode = 1_i32;
            let rounded = DWMWCP_ROUND;

            let backdrop_result = DwmSetWindowAttribute(
                window,
                DWMWA_SYSTEMBACKDROP_TYPE,
                (&backdrop as *const _).cast(),
                std::mem::size_of_val(&backdrop) as u32,
            );
            let _ = DwmSetWindowAttribute(
                window,
                DWMWA_USE_IMMERSIVE_DARK_MODE,
                (&dark_mode as *const _).cast(),
                std::mem::size_of_val(&dark_mode) as u32,
            );
            let _ = DwmSetWindowAttribute(
                window,
                DWMWA_WINDOW_CORNER_PREFERENCE,
                (&rounded as *const _).cast(),
                std::mem::size_of_val(&rounded) as u32,
            );

            if backdrop_result >= 0 {
                eprintln!("VantaEngine Editor: Windows Mica backdrop enabled.");
            } else {
                eprintln!("VantaEngine Editor: native Mica is unavailable; using the dark glass fallback.");
            }
        }
    }

    pub fn install() {
        let process_id = std::process::id();
        let _ = thread::Builder::new()
            .name("vanta-native-backdrop".into())
            .spawn(move || {
                for _ in 0..120 {
                    let mut search = WindowSearch {
                        process_id,
                        handle: None,
                    };
                    unsafe {
                        EnumWindows(
                            Some(find_editor_window),
                            &mut search as *mut WindowSearch as LPARAM,
                        );
                    }
                    if let Some(window) = search.handle {
                        apply_mica(window);
                        return;
                    }
                    thread::sleep(Duration::from_millis(100));
                }
                eprintln!("VantaEngine Editor: timed out waiting for the native window handle.");
            });
    }
}

#[cfg(windows)]
pub use platform::install;

#[cfg(not(windows))]
pub fn install() {}
