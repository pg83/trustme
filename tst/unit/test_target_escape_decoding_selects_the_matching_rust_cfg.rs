//@ input: support/targets/control_unicode_cfg_environment.toml
//@ reject-flags: --target=$DIR/support/targets/control_unicode_cfg_environment.toml || -Aexplicit_builtin_cfgs_in_flags --cfg 'target_env="escaped\n\r\u{8}\u{c}Eé你🚀"' => decoded_control_unicode_cfg || decoded_control_unicode_cfg
#[cfg(target_env = "escaped\n\r\u{8}\u{c}Eé你🚀")]
compile_error!("decoded_control_unicode_cfg");
fn main() {
    let chars = ['E', 'é', '你', '🚀'];
    let text: String = chars.into_iter().collect();
    assert_eq!(text.as_bytes(), b"E\xc3\xa9\xe4\xbd\xa0\xf0\x9f\x9a\x80");
    assert_eq!(chars.map(char::len_utf8), [1, 2, 3, 4]);
}
