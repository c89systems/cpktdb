include_guard(GLOBAL)
function(cpkt_literal_argument output value)
  set(_equals "")
  string(LENGTH "${value}" _length)
  string(FIND "${value}]${_equals}]" "]${_equals}]" _closing)
  while(_closing LESS _length)
    string(APPEND _equals "=")
    string(FIND "${value}]${_equals}]" "]${_equals}]" _closing)
  endwhile()
  # CMake ignores the first newline after a bracket opener. Supply that newline
  # ourselves so a value beginning with a newline retains it too.
  set(${output} "[${_equals}[\n${value}]${_equals}]" PARENT_SCOPE)
endfunction()
