fn inner() -> Result<i32, i32> {
    Err(15)
}

pub fn propagate() -> Result<i32, i32> {
    Ok(inner()? + 1)
}
