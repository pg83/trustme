//@ run-pass
// The closure returned as `impl FnMut(Sink<T>, T) -> Result<Sink<T>, ()>` is
// checked against every closure impl in scope, among them core's own generic
// closures whose where-clauses relate projections of parameters the head did
// not fix (`<I as Iterator>::Item: PartialEq<<J as IntoIterator>::Item>`).
// A goal on a projection over such an unknown is ambiguous outright; matching
// impls against it by guesswork must not happen, let alone leave behind
// unknowns that outlive the probe.
struct Sink<T> {
    dst: *mut T,
}

fn write_with<T>(src_end: *const T) -> impl FnMut(Sink<T>, T) -> Result<Sink<T>, ()> {
    move |mut sink, item| {
        unsafe {
            assert!(sink.dst as *const _ <= src_end, "contract violation");
            std::ptr::write(sink.dst, item);
            sink.dst = sink.dst.add(1);
        }
        Ok(sink)
    }
}

fn main() {
    let mut buf = [0u8; 2];
    let end = unsafe { buf.as_ptr().add(2) };
    let mut write = write_with(end);
    let sink = write(Sink { dst: buf.as_mut_ptr() }, 7u8).unwrap();
    let sink = write(sink, 9u8).unwrap();
    assert_eq!(sink.dst, end as *mut u8);
    assert_eq!(buf, [7, 9]);
}
