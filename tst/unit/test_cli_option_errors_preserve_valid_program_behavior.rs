//@ compile-flags: --cfg matrix_enabled --check-cfg=cfg(matrix_enabled) -Zfmt-debug=full -Zunstable-options --env-set=TRUSTME_DUMP=:ast:hir:mir --emit=link,metadata,dep-info=$OUT.d
//@ output-contains: .d test_cli_option_errors_preserve_valid_program_behavior.rs
//@ stdout-flags: --help => --crate-type
//@ stdout-flags: --version => rustc
//@ stdout-flags: -vV => rustc
//@ stdout-flags: --verbose --version => rustc
//@ stdout-flags: --print=cfg => matrix_enabled
//@ reject-flags: second-input.rs => Unexpected free argument || multiple input filenames
//@ reject-flags: -L => Option L requires an argument || Argument to option 'L' missing
//@ reject-flags: -l => Option l requires an argument || Argument to option 'l' missing
//@ reject-flags: -D => Option -D requires an argument || Argument to option 'D' missing
//@ reject-flags: -C => Option C requires an argument || Argument to option 'C' missing
//@ reject-flags: -Cpanic => Flag -C panic requires an argument || option `panic` requires
//@ reject-flags: -Coverflow-checks=maybe => invalid value for -C overflow-checks || codegen option `overflow-checks`
//@ reject-flags: -Cunknown-option=1 => Unknown codegen option || unknown codegen option
//@ reject-flags: -Z => Option Z requires an argument || Argument to option 'Z' missing
//@ reject-flags: -Zmir-opt-level= => Invalid number for -Z mir-opt-level || unstable option `mir-opt-level`
//@ reject-flags: -Zmir-opt-level=184467440737095516160 => Number for -Z mir-opt-level is too large || unstable option `mir-opt-level`
//@ reject-flags: -Zshare-generics=maybe => invalid value for -Z share-generics || unstable option `share-generics`
//@ reject-flags: -Zub-checks=maybe => invalid value for -Z ub-checks || unstable option `ub-checks`
//@ reject-flags: -Zfmt-debug=bad => invalid value for -Z fmt-debug || unstable option `fmt-debug`
//@ reject-flags: -Zlink-directives=maybe => invalid value for -Z link-directives || unstable option `link-directives`
//@ reject-flags: -Znext-solver=nope => Invalid value for -Z next-solver || unstable option `next-solver`
//@ reject-flags: --crate-type=nothing => Unknown value for --crate-type || unknown crate type
//@ reject-flags: --edition=2099 => Unknown value for --edition || argument for `--edition`
//@ reject-flags: -Zunstable-options --env-set=missing_equals => --env-set takes an argument || specify value for variable `missing_equals`
//@ reject-flags: --print=unknown => unknown print request || unknown print request
//@ reject-flags: -Q => Unrecognized option || Unrecognized option
//@ compile-variant: --cap-lints=deny -Copt-level=2 -Cdebuginfo=line-tables-only
//@ compile-variant: --cap-lints=forbid -Copt-level=z -Cdebug-assertions=no -Coverflow-checks=off -Zshare-generics=no -Zub-checks=no
//@ compile-variant: -Zlink-directives=yes -Zmir-opt-level=0 -Cdebug-assertions=yes -Coverflow-checks=on
//@ compile-variant: -C opt-level=0 -Cdebuginfo=none -A unused_mut -W dead_code -D unused_variables -F unsafe_code --cap-lints=warn
//@ compile-variant: -Copt-level=1 -Cdebuginfo=limited -Zshare-generics=yes -Zub-checks=yes -Zlink-directives=no --cap-lints=allow
//@ compile-variant: -Copt-level=3 -Cdebuginfo=full -Lnative=$DIR -Ldependency=$DIR -Lall=$DIR
//@ compile-variant: -Copt-level=s -Cdebuginfo=line-directives-only -C 'link-args=  -lc   -lm '
#[derive(Debug)]
struct Report { count: u8, values: [u8; 3] }
fn main() {
    assert!(cfg!(matrix_enabled));
    assert_eq!(env!("TRUSTME_DUMP"), ":ast:hir:mir");
    let report = Report { count: 37, values: [2, 3, 5] };
    assert_eq!(report.count.rotate_left(2), 148);
    assert_eq!(report.values.iter().copied().sum::<u8>(), 10);
    assert_eq!(format!("{:?}", report), "Report { count: 37, values: [2, 3, 5] }");
}
