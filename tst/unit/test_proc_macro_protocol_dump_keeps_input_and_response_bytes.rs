//@ proc-macro-aux-build: dump_identity.rs
//@ env: TRUSTME_DUMP_PROCMACRO=$OUT-proc

fn main() {
    let pair = dump_identity::identity!((37u16, 41u16));
    assert_eq!(pair, (37, 41));
    let prefix = std::env::var("TRUSTME_DUMP_PROCMACRO").unwrap();
    let prefix = std::path::Path::new(&prefix);
    let name_prefix = format!("{}-", prefix.file_name().unwrap().to_str().unwrap());
    for direction in ["out", "res"] {
        let suffix = format!("-{direction}.bin");
        let mut count = 0;
        for entry in std::fs::read_dir(prefix.parent().unwrap()).unwrap() {
            let entry = entry.unwrap();
            let name = entry.file_name();
            let name = name.to_str().unwrap();
            if name.starts_with(&name_prefix) && name.ends_with(&suffix) {
                assert!(!std::fs::read(entry.path()).unwrap().is_empty());
                count += 1;
            }
        }
        assert_eq!(count, 1);
    }
}
