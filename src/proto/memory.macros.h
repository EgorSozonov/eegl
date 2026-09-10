#define getRefCount(a) _Generic((a),\
   Job*: _getRefCount\
)(a);
#define incRefCount(a) _Generic((a),\
   Job*: _incRefCount\
)(a);
#define decRefCount(a) _Generic((a),\
   Job*: _decRefCount\
)(a);
