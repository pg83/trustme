use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

pub struct Fixture {
    pub directory: PathBuf,
    sources: PathBuf,
    standard_library: PathBuf,
}

impl Fixture {
    pub fn new(source: &str, name: &str) -> Self {
        let directory = env::temp_dir().join(format!("{}_{}", name, std::process::id()));
        fs::create_dir_all(&directory).unwrap();
        Self {
            directory,
            sources: Path::new(source).parent().unwrap().join("aux"),
            standard_library: env::current_exe().unwrap().parent().unwrap().join("libstd/release"),
        }
    }

    pub fn compile(&self, input: &str, output: &str, kind: &str, flags: &[&str]) {
        let mut command = Command::new(env::var_os("RUSTC").unwrap());
        command.arg(self.sources.join(input))
            .args(["--edition=2021", "--crate-type", kind])
            .arg("-L").arg(&self.standard_library)
            .arg("-L").arg(&self.directory)
            .args(["-Ctarget-feature=-crt-static", "-Clink-arg=-fPIC"])
            .args(flags)
            .arg("-o").arg(self.directory.join(output));
        Self::checked(&mut command);
    }

    pub fn compile_c(&self, input: &str, output: &str) {
        let mut command = Command::new(env::var_os("CC").unwrap_or_else(|| "cc".into()));
        command.args(["-shared", "-fPIC", "-nostdlib"])
            .arg(self.sources.join(input))
            .arg("-o").arg(self.directory.join(output));
        Self::checked(&mut command);
    }

    pub fn execute(&self, name: &str) {
        let mut paths = vec![self.directory.clone()];
        if let Some(previous) = env::var_os("LD_LIBRARY_PATH") {
            paths.extend(env::split_paths(&previous));
        }
        let mut command = Command::new(self.directory.join(name));
        command.env("LD_LIBRARY_PATH", env::join_paths(paths).unwrap());
        Self::checked(&mut command);
    }

    fn checked(command: &mut Command) {
        let output = command.output().unwrap();
        println!("command: {:?}\nstatus: {}\n{}{}", command, output.status,
            String::from_utf8_lossy(&output.stdout), String::from_utf8_lossy(&output.stderr));
        assert!(output.status.success());
    }
}

impl Drop for Fixture {
    fn drop(&mut self) { let _ = fs::remove_dir_all(&self.directory); }
}
