#[inline]
pub fn classify(value: &str) -> u8 {
    match value { "" => 3, "birch" => 5, "cedar" => 7, "elm" => 11, _ => 13 }
}
