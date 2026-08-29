//@ check-pass
//@ crate-type: lib

use std::io::Read;

#[derive(PartialEq)]
enum Version {
    One,
    Two,
}

fn classify() -> Option<(Vec<u8>, Version)> {
    let mut input = Vec::with_capacity(128);
    std::fs::File::open("/proc/self/cgroup")
        .ok()?
        .read_to_end(&mut input)
        .ok()?;
    input
        .split(|&byte| byte == b'\n')
        .fold(None, |previous, line| {
            let mut fields = line.splitn(3, |&byte| byte == b':');
            let version = match fields.nth(1) {
                Some(b"") => Version::Two,
                Some(controllers)
                    if std::str::from_utf8(controllers)
                        .is_ok_and(|text| text.split(',').any(|item| item == "cpu")) =>
                {
                    Version::One
                }
                _ => return previous,
            };
            if previous.is_some() && version == Version::Two {
                return previous;
            }
            let path = fields.last()?;
            Some((path[1..].to_owned(), version))
        })
}
