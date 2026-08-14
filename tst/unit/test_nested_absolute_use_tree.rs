fn main() {
    use {::{}};
    use {{::{{core as core2}}}};

    assert_eq!(core2::mem::size_of::<u32>(), 4);
}
