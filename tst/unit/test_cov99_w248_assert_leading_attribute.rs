fn main() {
    assert!({
        #[cfg(any())]
        #[::w248_unexpanded]
        let _ = ();
        true
    });
}
