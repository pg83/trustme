//@ test-harness

#[test]
fn test_harness_reports_unwind_strategy() {
    assert!(cfg!(panic = "unwind"));
    assert!(!cfg!(panic = "abort"));
}
