//@ compile-flags: --env-set TRUSTME_DRIVER_ENV=123abc -Zunstable-options -C debug_assertions=no -C target-feature=-crt-static -Clink-args=-Wl,-z,text

#[cfg(debug_assertions)]
compile_error!("-C debug_assertions=no was ignored");

#[cfg(target_feature = "crt-static")]
compile_error!("-C target-feature=-crt-static was ignored");

const _: [(); 6] = [(); env!("TRUSTME_DRIVER_ENV").len()];

fn main() {
    assert_eq!(env!("TRUSTME_DRIVER_ENV"), "123abc");
}
