//@ input: aux/own_pause_after_start_client.rs
use std::io::Write;
use std::process::{Command, Stdio};

fn main() {
    let executable = std::env::current_exe().unwrap();
    let work = executable.parent().unwrap();
    let artifacts = work.join("stdin-artifacts");
    std::fs::create_dir(&artifacts).unwrap();
    let source = include_str!("aux/own_pause_after_start_client.rs");
    let input = artifacts.join("input.rs");
    std::fs::write(&input, source).unwrap();
    let client = artifacts.join("client");
    let mut command = Command::new(std::env::var_os("RUSTC").unwrap());
    command.args(["-Z", "pause-after-start", "--edition=2021"])
        .arg(&input).arg("-L").arg(work.join("libstd/release"))
        .arg("-o").arg(&client)
        .stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::piped());
    std::fs::write(artifacts.join("command.txt"), format!("{:?}", command)).unwrap();
    let mut child = command.spawn().unwrap();
    std::fs::write(artifacts.join("compiler-pid.txt"), child.id().to_string()).unwrap();
    child.stdin.take().unwrap().write_all(b"x\n").unwrap();
    let output = child.wait_with_output().unwrap();
    std::fs::write(artifacts.join("compiler.stdout"), &output.stdout).unwrap();
    std::fs::write(artifacts.join("compiler.stderr"), &output.stderr).unwrap();
    std::fs::write(artifacts.join("compiler-status.txt"), format!("{:?}", output.status)).unwrap();
    assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
    assert!(String::from_utf8_lossy(&output.stderr).contains("Pausing to attach a debugger"));
    let output = Command::new(&client).output().unwrap();
    std::fs::write(artifacts.join("client.stdout"), &output.stdout).unwrap();
    std::fs::write(artifacts.join("client.stderr"), &output.stderr).unwrap();
    std::fs::write(artifacts.join("client-status.txt"), format!("{:?}", output.status)).unwrap();
    assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
    assert_eq!(output.stdout, b"9\n");
}
