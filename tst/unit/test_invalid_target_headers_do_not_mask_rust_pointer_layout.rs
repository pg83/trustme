//@ input: support/targets/round261_header_eof.toml
//@ input: support/targets/round261_header_open.toml
//@ input: support/targets/round261_header_close.toml
//@ input: support/targets/round261_header_brace_open.toml
//@ input: support/targets/round261_header_brace_close.toml
//@ input: support/targets/round261_header_assign.toml
//@ input: support/targets/round261_header_newline.toml
//@ input: support/targets/round261_header_comma.toml
//@ input: support/targets/round261_header_dot.toml
//@ input: support/targets/round261_header_string.toml
//@ reject-flags: --target=$DIR/support/targets/round261_header_eof.toml => Unexpected token in block name - Eof || error loading target specification
//@ reject-flags: --target=$DIR/support/targets/round261_header_open.toml => Unexpected token in block name - SquareOpen || error loading target specification
//@ reject-flags: --target=$DIR/support/targets/round261_header_close.toml => Unexpected token in block name - SquareClose || error loading target specification
//@ reject-flags: --target=$DIR/support/targets/round261_header_brace_open.toml => Unexpected token in block name - BraceOpen || error loading target specification
//@ reject-flags: --target=$DIR/support/targets/round261_header_brace_close.toml => Unexpected token in block name - BraceClose || error loading target specification
//@ reject-flags: --target=$DIR/support/targets/round261_header_assign.toml => Unexpected token in block name - Assign || error loading target specification
//@ reject-flags: --target=$DIR/support/targets/round261_header_newline.toml => Unexpected token in block name - Newline || error loading target specification
//@ reject-flags: --target=$DIR/support/targets/round261_header_comma.toml => Unexpected token in block name - Comma || error loading target specification
//@ reject-flags: --target=$DIR/support/targets/round261_header_dot.toml => Unexpected token in block name - Dot || error loading target specification
//@ reject-flags: --target=$DIR/support/targets/round261_header_string.toml => Unexpected token after block block - String(after-table) || error loading target specification

fn main() {
    let words = [17u16, 31, 47];
    let array = &words as *const [u16; 3];
    let slice = array as *const [u16];
    assert_eq!(slice.len(), 3);
    assert_eq!(slice as *const u16, words.as_ptr());
    assert_eq!(std::mem::size_of_val(&words), 6);
    assert_eq!(usize::BITS as usize, std::mem::size_of::<usize>() * 8);
    assert_eq!(words[2], 47);
}
