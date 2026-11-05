//@ input: support/targets/unicode_scalar_boundaries.toml
//@ input: support/targets/unicode_escape_bad_hex.toml
//@ input: support/targets/unicode_escape_eof.toml
//@ input: support/targets/unicode_escape_low_surrogate.toml
//@ input: support/targets/unicode_escape_high_surrogate.toml
//@ input: support/targets/unicode_escape_above_max.toml
//@ input: support/targets/unicode_escape_max_u32.toml
//@ reject-flags: --target=$DIR/support/targets/unicode_scalar_boundaries.toml || -Aexplicit_builtin_cfgs_in_flags --cfg 'target_env="bounds\u{0}\u{7f}\u{80}\u{7ff}\u{800}\u{d7ff}\u{e000}\u{ffff}\u{10000}\u{10ffff}"' => decoded_scalar_boundaries || decoded_scalar_boundaries
//@ reject-flags: --target=$DIR/support/targets/unicode_escape_bad_hex.toml || -Aexplicit_builtin_cfgs_in_flags --cfg 'target_env="\u{12x4}"' => Invalid hex digit in Unicode escape || invalid `--cfg` argument
//@ reject-flags: --target=$DIR/support/targets/unicode_escape_eof.toml || -Aexplicit_builtin_cfgs_in_flags --cfg 'target_env="\u{12"' => Unexpected EOF in Unicode escape || error
//@ reject-flags: --target=$DIR/support/targets/unicode_escape_low_surrogate.toml || -Aexplicit_builtin_cfgs_in_flags --cfg 'target_env="\u{d800}"' => Invalid Unicode scalar in string escape || invalid `--cfg` argument
//@ reject-flags: --target=$DIR/support/targets/unicode_escape_high_surrogate.toml || -Aexplicit_builtin_cfgs_in_flags --cfg 'target_env="\u{dfff}"' => Invalid Unicode scalar in string escape || invalid `--cfg` argument
//@ reject-flags: --target=$DIR/support/targets/unicode_escape_above_max.toml || -Aexplicit_builtin_cfgs_in_flags --cfg 'target_env="\u{110000}"' => Invalid Unicode scalar in string escape || invalid `--cfg` argument
//@ reject-flags: --target=$DIR/support/targets/unicode_escape_max_u32.toml || -Aexplicit_builtin_cfgs_in_flags --cfg 'target_env="\u{ffffffff}"' => Invalid Unicode scalar in string escape || invalid `--cfg` argument
#[cfg(target_env = "bounds\u{0}\u{7f}\u{80}\u{7ff}\u{800}\u{d7ff}\u{e000}\u{ffff}\u{10000}\u{10ffff}")]
compile_error!("decoded_scalar_boundaries");
fn main() {
    let chars = ['\u{0}','\u{7f}','\u{80}','\u{7ff}','\u{800}','\u{d7ff}','\u{e000}','\u{ffff}','\u{10000}','\u{10ffff}'];
    assert_eq!(chars.map(char::len_utf8), [1, 1, 2, 2, 3, 3, 3, 3, 4, 4]);
    assert_eq!(chars[9], char::MAX);
}
