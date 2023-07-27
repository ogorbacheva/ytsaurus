#if !defined(SWIG_STD_STRING) 
#define SWIG_STD_BASIC_STRING

%include <pycontainer.swg>

#define %swig_basic_string(Type...)  %swig_sequence_methods_val(Type)


%fragment(SWIG_AsPtr_frag(std::basic_string<char>),"header",
	  fragment="SWIG_AsCharPtrAndSize") {
SWIGINTERN int
SWIG_AsPtr(std::basic_string<char>)(PyObject* obj, std::string **val) {
  static swig_type_info* string_info = SWIG_TypeQuery("std::basic_string<char> *");
  std::string *vptr;
  if (SWIG_IsOK(SWIG_ConvertPtr(obj, (void**)&vptr, string_info, 0))) {
    if (val) *val = vptr;
    return SWIG_OLDOBJ;
  } else {
    PyErr_Clear();
    char* buf = 0 ; size_t size = 0; int alloc = 0;
    if (SWIG_IsOK(SWIG_AsCharPtrAndSize(obj, &buf, &size, &alloc))) {
      if (buf) {
	if (val) *val = new std::string(buf, size - 1);
	if (alloc == SWIG_NEWOBJ) %delete_array(buf);
	return SWIG_NEWOBJ;
      } else {
        if (val) *val = 0;
        return SWIG_OLDOBJ;
      }
    }
    return SWIG_ERROR;
  }
}
}

%fragment(SWIG_From_frag(std::basic_string<char>),"header",
	  fragment="SWIG_FromCharPtrAndSize") {
SWIGINTERNINLINE PyObject*
  SWIG_From(std::basic_string<char>)(const std::string& s) {
    return SWIG_FromCharPtrAndSize(s.data(), s.size());
  }
}

%include <std/std_basic_string.i>
%typemaps_asptrfromn(%checkcode(STRING), std::basic_string<char>);

#endif


#if !defined(SWIG_STD_WSTRING)

%fragment(SWIG_AsPtr_frag(std::basic_string<wchar_t>),"header",
	  fragment="SWIG_AsWCharPtrAndSize") {
SWIGINTERN int
SWIG_AsPtr(std::basic_string<wchar_t>)(PyObject* obj, std::wstring **val) {
  static swig_type_info* string_info = SWIG_TypeQuery("std::basic_string<wchar_t> *");
  std::wstring *vptr;
  if (SWIG_IsOK(SWIG_ConvertPtr(obj, (void**)&vptr, string_info, 0))) {
    if (val) *val = vptr;
    return SWIG_OLDOBJ;
  } else {
    PyErr_Clear();
    wchar_t *buf = 0 ; size_t size = 0; int alloc = 0;
    if (SWIG_IsOK(SWIG_AsWCharPtrAndSize(obj, &buf, &size, &alloc))) {
      if (buf) {
        if (val) *val = new std::wstring(buf, size - 1);
        if (alloc == SWIG_NEWOBJ) %delete_array(buf);
        return SWIG_NEWOBJ;
      } else {
        if (val) *val = 0;
        return SWIG_OLDOBJ;
      }
    }
    return SWIG_ERROR;
  }
}
}

%fragment(SWIG_From_frag(std::basic_string<wchar_t>),"header",
	  fragment="SWIG_FromWCharPtrAndSize") {
SWIGINTERNINLINE PyObject*
  SWIG_From(std::basic_string<wchar_t>)(const std::wstring& s) {
    return SWIG_FromWCharPtrAndSize(s.data(), s.size());
  }
}

%typemaps_asptrfromn(%checkcode(UNISTRING), std::basic_string<wchar_t>);

#endif
