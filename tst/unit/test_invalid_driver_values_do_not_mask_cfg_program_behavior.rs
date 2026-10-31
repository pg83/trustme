//@ compile-flags: --cfg 'feature="named"' --cfg 'driver_mode="checked"' --check-cfg 'cfg(feature, values("named"))' --check-cfg 'cfg(driver_mode, values("checked"))'
//@ reject-flags: -Copt-level=7 => optimization level needs to be || optimization level needs to be
//@ reject-flags: -Cdebug-assertions=maybe => invalid value for -C debug-assertions || codegen option `debug-assertions`
//@ reject-flags: -Cdebuginfo=bad => invalid value for -C debuginfo || codegen option `debuginfo`
//@ reject-flags: -Zmir-opt-level=abc => Invalid number for -Z mir-opt-level || unstable option `mir-opt-level`
//@ reject-flags: -L '' => Option -L requires a non-empty path || empty search path given via `-L`
//@ reject-flags: -Lall= => Option -L requires a non-empty path || empty search path given via `-L`
//@ reject-flags: -Lcrate= => Option -L requires a non-empty path || empty search path given via `-L`
//@ reject-flags: -Ldependency= => Option -L requires a non-empty path || empty search path given via `-L`
//@ reject-flags: -Lnative= => Option -L requires a non-empty path || empty search path given via `-L`
//@ reject-flags: -Lframework= => Option -L requires a non-empty path || empty search path given via `-L`
//@ reject-flags: -o => Option -o requires an argument || Argument to option 'o' missing
//@ reject-flags: --out-dir => requires an argument || Argument to option 'out-dir' missing
//@ reject-flags: --extern => requires an argument || Argument to option 'extern' missing
//@ reject-flags: --target => requires an argument || Argument to option 'target' missing
//@ reject-flags: --cap-lints=bad => unknown lint level || unknown lint level
//@ reject-flags: --error-format=bad => argument for `--error-format` || argument for `--error-format`
//@ reject-flags: --no-such-option=value => Unrecognized option || Unrecognized option
//@ reject-flags: --crate => Option --crate requires an argument || Unrecognized option
//@ reject-flags: --crate missing_equals => requires <unique-name>=<metadata-path> || Unrecognized option
//@ reject-flags: --crate =missing_name => requires <unique-name>=<metadata-path> || Unrecognized option
//@ reject-flags: --crate name= => requires <unique-name>=<metadata-path> || Unrecognized option
//@ reject-flags: --crate-object => Option --crate-object requires an argument || Unrecognized option
//@ reject-flags: --crate-object missing_equals => requires <unique-name>=<object-path> || Unrecognized option
//@ reject-flags: --proc-macro => Option --proc-macro requires an argument || Unrecognized option
//@ reject-flags: --proc-macro missing_equals => requires <unique-name>=<executable-path> || Unrecognized option
//@ reject-flags: --crate-alias => Option --crate-alias requires an argument || Unrecognized option
//@ reject-flags: --crate-alias missing_equals => requires <source-name>=<unique-name> || Unrecognized option
//@ reject-flags: --dump-target-spec => Flag --dump-target-spec requires an argument || Unrecognized option
//@ reject-flags: -Zstop-after=missing => Unknown argument to -Z stop-after || unknown unstable option
//@ reject-flags: -Zdump-ast=yes => doesn't take an argument || unknown unstable option
//@ compile-variant: --error-format=human
//@ compile-variant: --error-format=short
//@ compile-variant: --error-format=json
fn main() {
    assert!(cfg!(feature = "named"));
    assert!(cfg!(driver_mode = "checked"));
    let values = [17u16, 23, 37];
    assert_eq!(values.iter().copied().sum::<u16>(), 77);
}
