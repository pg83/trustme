#![feature(no_core)]
#![no_core]
#![crate_type = "lib"]

#[cfg(not(driver_flag))]
compile_error!("--cfg=driver_flag did not enable the flag");

#[cfg(not(driver_value = "active"))]
compile_error!("--cfg driver_value=\"active\" did not enable the value");

#[cfg(driver_unknown)]
pub fn selected_by_unknown_name() {}

#[cfg(driver_value = "unexpected")]
pub fn selected_by_unknown_value() {}
