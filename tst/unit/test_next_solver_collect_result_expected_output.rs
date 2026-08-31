use std::collections::HashMap;

fn collect_strings(
    names: &[&str],
    bytes: &[u8],
) -> Result<HashMap<String, Vec<u8>>, String> {
    let strings: HashMap<String, Vec<u8>> = match (0..names.len())
        .map(|index| {
            let name = names[index];
            if bytes.is_empty() {
                return Ok((name.to_string(), Vec::new()));
            }

            match bytes.iter().position(|&byte| byte == 0) {
                Some(len) => Ok((name.to_string(), bytes[..len].to_vec())),
                None => Err("missing terminator".to_string()),
            }
        })
        .collect()
    {
        Ok(value) => value,
        Err(error) => return Err(error.to_string()),
    };
    Ok(strings)
}

fn main() {
    let result = collect_strings(&["name"], &[b'x', 0]).unwrap();
    assert_eq!(result["name"], b"x");
}
