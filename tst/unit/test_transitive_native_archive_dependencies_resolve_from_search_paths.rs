//@ native-static-aux-build: roundtrip_native_archive.rs
//@ aux-build: transitive_native_archive_user.rs
extern crate transitive_native_archive_user;
fn main() {
    assert_eq!(transitive_native_archive_user::compute(37), 113);
    assert_eq!(transitive_native_archive_user::compute(0), 2);
}
