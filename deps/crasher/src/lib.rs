use crash_handler;
use libc;
use minidump_writer;

/*
use core::sync::atomic::{AtomicUsize, Ordering};

static CRASH_COUNT: AtomicUsize = AtomicUsize::new(0);

if CRASH_COUNT.load(Ordering::SeqCst) > 0 {
    println!("Crashed while crashing");
    libc::_exit(67);
}
CRASH_COUNT.fetch_add(1, Ordering::SeqCst);
*/

#[allow(deprecated)]
#[no_mangle]
pub extern "C" fn crasher_install() {
    unsafe {
        let ch = crash_handler::CrashHandler::attach(crash_handler::make_crash_event(
            move |cc: &crash_handler::CrashContext| {
                let child = libc::fork();
                if child == 0 {
                    //Why so much alloc...
                    let mut writer =
                        minidump_writer::minidump_writer::MinidumpWriter::new(cc.pid, cc.tid);
                    writer.set_crash_context(minidump_writer::crash_context::CrashContext {
                        inner: cc.clone(),
                    });
                    let Ok(mut minidump_file) = std::fs::File::create("example_dump.mdmp") else {
                        println!("failed to create file");
                        libc::_exit(1);
                    };
                    if let Err(e) = writer.dump(&mut minidump_file) {
                        println!("failed to write minidump: {}", e);
                        libc::_exit(1)
                    };

                    libc::_exit(0);
                }
                let mut status = 0;
                libc::waitpid(child, &mut status, 0);
                return crash_handler::CrashEventResult::Handled(true); // Crash for real.
            },
        ))
        .unwrap();
        std::mem::forget(ch);
    }
}
