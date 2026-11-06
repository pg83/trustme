//@ env: TRUSTME_OPTION_ENV_UNIT_VALUE=mode=small with spaces
//@ env: TRUSTME_OPTION_ENV_UNIT_EMPTY=

const SETTING: Option<&str> = option_env!("TRUSTME_OPTION_ENV_UNIT_VALUE");
const EMPTY: Option<&str> = option_env!("TRUSTME_OPTION_ENV_UNIT_EMPTY");

fn main() {
    assert_eq!(SETTING, Some("mode=small with spaces"));
    assert_eq!(EMPTY, Some(""));
    assert_eq!(SETTING, Some(env!("TRUSTME_OPTION_ENV_UNIT_VALUE")));
}
