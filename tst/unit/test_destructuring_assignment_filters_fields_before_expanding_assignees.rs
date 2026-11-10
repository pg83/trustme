struct Record {
    left: u16,
    right: u16,
}

fn main() {
    let mut first = 0;
    let mut second = 0;
    Record {
        #[cfg(any())]
        absent: missing_assignee!(),
        #[cfg(all())]
        left: first,
        #[allow(unused)]
        right: second,
        ..
    } = Record { left: 17, right: 23 };
    assert_eq!((first, second), (17, 23));

    Record {
        #[cfg_attr(all(), cfg(any()))]
        hidden: unresolved_destination,
        #[cfg_attr(all(), allow(unused))]
        left: first,
        ..
    } = Record { left: 31, right: 47 };
    assert_eq!((first, second), (31, 23));
}
