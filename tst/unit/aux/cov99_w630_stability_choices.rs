#![no_std]
#![feature(staged_api)]
#![allow(internal_features)]
#![stable(feature = "cov99_w630_aux", since = "1.0.0")]

#[stable(feature = "cov99_w630_aux", since = "1.0.0")]
pub trait FirstDenied {
    #[unstable(feature = "cov99_w630_first_denied", issue = "none")]
    fn cov99_w630_pick(&self) -> u8 { 1 }
}

#[stable(feature = "cov99_w630_aux", since = "1.0.0")]
pub trait SecondDenied {
    #[unstable(feature = "cov99_w630_second_denied", issue = "none")]
    fn cov99_w630_pick(&self) -> u8 { 2 }
}

#[stable(feature = "cov99_w630_aux", since = "1.0.0")]
impl FirstDenied for u8 {}

#[stable(feature = "cov99_w630_aux", since = "1.0.0")]
impl SecondDenied for u8 {}
