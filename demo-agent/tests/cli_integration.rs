//! Integration tests for the robotops-demo-agent binary.
//!
//! These tests invoke the compiled binary as a subprocess. --help and --version
//! exit before ROS2 initialisation, so no ROS2 stack is required.

use std::path::PathBuf;
use std::process::Command;

fn binary_path() -> PathBuf {
    // cargo test places the binary alongside the test executable in target/{profile}/
    let mut path = std::env::current_exe()
        .unwrap()
        .parent() // deps/
        .unwrap()
        .parent() // debug/ or release/
        .unwrap()
        .to_path_buf();
    path.push("robotops-demo-agent");
    path
}

#[test]
fn help_flag_succeeds() {
    let output = Command::new(binary_path())
        .arg("--help")
        .output()
        .expect("failed to execute binary");

    assert!(output.status.success(), "expected exit 0 for --help");
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("robotops-demo-agent"),
        "help should mention binary name"
    );
    assert!(
        stdout.contains("--output"),
        "help should mention --output flag"
    );
    assert!(
        stdout.contains("--limit-mb"),
        "help should mention --limit-mb flag"
    );
}

#[test]
fn version_flag_succeeds() {
    let output = Command::new(binary_path())
        .arg("--version")
        .output()
        .expect("failed to execute binary");

    assert!(output.status.success(), "expected exit 0 for --version");
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("robotops-demo-agent"),
        "version output should contain binary name"
    );
    // Version string should look like X.Y.Z
    assert!(
        stdout.contains('.'),
        "version output should contain a semver string"
    );
}

#[test]
fn invalid_flag_fails() {
    let output = Command::new(binary_path())
        .arg("--nonexistent-flag")
        .output()
        .expect("failed to execute binary");

    assert!(
        !output.status.success(),
        "expected non-zero exit for unknown flag"
    );
}

#[test]
fn invalid_limit_mb_fails() {
    let output = Command::new(binary_path())
        .args(["--limit-mb", "not-a-number"])
        .output()
        .expect("failed to execute binary");

    assert!(
        !output.status.success(),
        "expected non-zero exit for invalid --limit-mb"
    );
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("invalid value") || stderr.contains("error"),
        "stderr should describe the parse error, got: {stderr}"
    );
}
