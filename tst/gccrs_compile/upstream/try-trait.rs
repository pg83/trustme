fn parse_number(text: &str) -> Result<i32, std::num::ParseIntError> {
    let value = text.parse::<i32>()?;
    Ok(value + 1)
}

pub fn test() -> Result<i32, std::num::ParseIntError> {
    parse_number("41")
}
