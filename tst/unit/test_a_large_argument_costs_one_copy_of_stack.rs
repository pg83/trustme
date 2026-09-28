// stacker's test recurses with a 50 000-byte array in each frame, handed on by
// value, inside a red zone sized for rustc's frames. rustc passes a Rust-ABI
// argument larger than two pointers by pointer to one copy; the C backend's
// by-value struct cost clang at -O0 a temporary and the outgoing-argument
// area besides, and eight levels no longer fitted in a 1 MiB stack.
#[inline(never)]
fn consume(x: [u8; 50000]) -> usize {
    x[0] as usize + x[49999] as usize
}

#[inline(never)]
fn recurse(n: usize) -> usize {
    let x = [n as u8; 50000];
    let deeper = if n != 0 { recurse(n - 1) } else { 0 };
    consume(x) + deeper
}

fn main() {
    let worker = std::thread::Builder::new().stack_size(1 << 20).spawn(|| recurse(7)).unwrap();
    assert_eq!(worker.join().unwrap(), 56);
}
