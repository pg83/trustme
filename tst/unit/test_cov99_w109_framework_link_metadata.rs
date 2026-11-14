//@ compile-only
//@ compile-flags: -Cemit-build-command=$OUT.command -Cemit-link-manifest=$OUT.manifest
//@ output-contains: .manifest framework=Coverage99W109

#[link(name = "framework=Coverage99W109")]
extern "C" {}

fn main() {}
