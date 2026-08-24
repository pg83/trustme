pub struct Big {
    _fields: [Option<Box<u8>>; 48],
}

fn make_big() -> Big {
    if std::hint::black_box(false) {
        panic!();
    }
    Big { _fields: [const { None }; 48] }
}

#[inline]
fn push(out: &mut Vec<Big>) {
    out.push(make_big());
}

#[inline(never)]
fn probe(base: *mut Vec<Big>) {
    let mut marker = Vec::<Big>::new();
    std::hint::black_box(&mut marker);
    let used = (base as usize).abs_diff(&mut marker as *mut Vec<Big> as usize);
    if used > 4096 {
        std::process::exit(1);
    }
}

#[inline(never)]
pub fn call_many(out: &mut Vec<Big>) {
    push(out);
    push(out);
    push(out);
    push(out);
    push(out);
    push(out);
    push(out);
    push(out);
    probe(out);
}

fn main() {
    let mut values = Vec::new();
    std::hint::black_box(&mut values);
    call_many(&mut values);
    assert_eq!(values.len(), 8);
}
