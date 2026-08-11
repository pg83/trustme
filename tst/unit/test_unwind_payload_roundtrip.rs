use std::panic::{catch_unwind, panic_any, resume_unwind, AssertUnwindSafe};

fn main() {
    let payload = catch_unwind(|| panic!("static panic payload")).unwrap_err();
    assert_eq!(
        *payload.downcast::<&'static str>().unwrap(),
        "static panic payload"
    );

    let payload = catch_unwind(|| panic!("{}", "formatted panic payload")).unwrap_err();
    assert_eq!(*payload.downcast::<String>().unwrap(), "formatted panic payload");

    let payload = catch_unwind(|| panic_any(0x1234_u32)).unwrap_err();
    assert_eq!(*payload.downcast::<u32>().unwrap(), 0x1234);

    let payload = catch_unwind(AssertUnwindSafe(|| {
        let payload = catch_unwind(|| panic_any(0x5678_u32)).unwrap_err();
        resume_unwind(payload);
    }))
    .unwrap_err();
    assert_eq!(*payload.downcast::<u32>().unwrap(), 0x5678);

    let payload = catch_unwind(|| {
        assert!(false, "assert panic payload");
    })
    .unwrap_err();
    assert_eq!(
        *payload.downcast::<&'static str>().unwrap(),
        "assert panic payload"
    );
}
