#include <assert.h>
#include <string.h>
#include "scriptdictionary.h"
#include "../scriptarray/scriptarray.h"

BEGIN_AS_NAMESPACE

using namespace std;

//--------------------------------------------------------------------------
// CScriptDictionary implementation

CScriptDictionary *CScriptDictionary::Create(asIScriptEngine *engine)
{
	// Use the custom memory routine from AngelScript to allow application to better control how much memory is used
	CScriptDictionary *obj = (CScriptDictionary*)asAllocMem(sizeof(CScriptDictionary));
	new(obj) CScriptDictionary(engine);
	return obj;
}

CScriptDictionary *CScriptDictionary::Create(asBYTE *buffer)
{
	// Use the custom memory routine from AngelScript to allow application to better control how much memory is used
	CScriptDictionary *obj = (CScriptDictionary*)asAllocMem(sizeof(CScriptDictionary));
	new(obj) CScriptDictionary(buffer);
	return obj;
}

CScriptDictionary::CScriptDictionary(asIScriptEngine *engine)
{
	// We start with one reference
	refCount = 1;
	gcFlag = false;

	// Keep a reference to the engine for as long as we live
	// We don't increment the reference counter, because the 
	// engine will hold a pointer to the object. 
	this->engine = engine;

	// Notify the garbage collector of this object
	// TODO: The object type should be cached
	engine->NotifyGarbageCollectorOfNewObject(this, engine->GetObjectTypeByName("dictionary"));
}

CScriptDictionary::CScriptDictionary(asBYTE *buffer)
{
	// We start with one reference
	refCount = 1;
	gcFlag = false;

	// This constructor will always be called from a script
	// so we can get the engine from the active context
	asIScriptContext *ctx = asGetActiveContext();
	engine = ctx->GetEngine();

	// Notify the garbage collector of this object
	// TODO: The type id should be cached
	engine->NotifyGarbageCollectorOfNewObject(this, engine->GetObjectTypeByName("dictionary"));

	// Initialize the dictionary from the buffer
	asUINT length = *(asUINT*)buffer;
	buffer += 4;

	while( length-- )
	{
		// Align the buffer pointer on a 4 byte boundary in 
		// case previous value was smaller than 4 bytes
		if( asPWORD(buffer) & 0x3 )
			buffer += 4 - (asPWORD(buffer) & 0x3);

		// Get the name value pair from the buffer and insert it in the dictionary
		string name = *(string*)buffer;
		buffer += sizeof(string);

		// Get the type id of the value
		int typeId = *(int*)buffer;
		buffer += sizeof(int);

		// Depending on the type id, the value will inline in the buffer or a pointer
		void *ref = (void*)buffer;

		if( typeId >= asTYPEID_INT8 && typeId <= asTYPEID_DOUBLE )
		{
			// Convert primitive values to either int64 or double, so we can use the overloaded Set methods
			asINT64 i64;
			double d;
			switch( typeId )
			{
			case asTYPEID_INT8: i64 = *(char*)ref; break;
			case asTYPEID_INT16: i64 = *(short*)ref; break;
			case asTYPEID_INT32: i64 = *(int*)ref; break;
			case asTYPEID_INT64: i64 = *(asINT64*)ref; break;
			case asTYPEID_UINT8: i64 = *(unsigned char*)ref; break;
			case asTYPEID_UINT16: i64 = *(unsigned short*)ref; break;
			case asTYPEID_UINT32: i64 = *(unsigned int*)ref; break;
			case asTYPEID_UINT64: i64 = *(asINT64*)ref; break;
			case asTYPEID_FLOAT: d = *(float*)ref; break;
			case asTYPEID_DOUBLE: d = *(double*)ref; break;
			}
			
			if( typeId >= asTYPEID_FLOAT )
				Set(name, d);
			else
				Set(name, i64);
		}
		else
		{
			if( (typeId & asTYPEID_MASK_OBJECT) && 
				!(typeId & asTYPEID_OBJHANDLE) && 
				(engine->GetObjectTypeById(typeId)->GetFlags() & asOBJ_REF) )
			{
				// Dereference the pointer to get the reference to the actual object
				ref = *(void**)ref;
			}

			Set(name, ref, typeId);
		}

		// Advance the buffer pointer with the size of the value
		if( typeId & asTYPEID_MASK_OBJECT )
		{
			asIObjectType *ot = engine->GetObjectTypeById(typeId);
			if( ot->GetFlags() & asOBJ_VALUE )
				buffer += ot->GetSize();
			else
				buffer += sizeof(void*);
		}
		else if( typeId == 0 )
		{
			// null pointer
			buffer += sizeof(void*);
		}
		else
		{
			buffer += engine->GetSizeOfPrimitiveType(typeId);
		}
	}
}

CScriptDictionary::~CScriptDictionary()
{
	// Delete all keys and values
	DeleteAll();
}

void CScriptDictionary::AddRef() const
{
	// We need to clear the GC flag
	gcFlag = false;
	asAtomicInc(refCount);
}

void CScriptDictionary::Release() const
{
	// We need to clear the GC flag
	gcFlag = false;
	if( asAtomicDec(refCount) == 0 )
	{
		this->~CScriptDictionary();
		asFreeMem(const_cast<CScriptDictionary*>(this));
	}
}

int CScriptDictionary::GetRefCount()
{
	return refCount;
}

void CScriptDictionary::SetGCFlag()
{
	gcFlag = true;
}

bool CScriptDictionary::GetGCFlag()
{
	return gcFlag;
}

void CScriptDictionary::EnumReferences(asIScriptEngine *engine)
{
	// Call the gc enum callback for each of the objects
	map<string, CScriptDictValue>::iterator it;
	for( it = dict.begin(); it != dict.end(); it++ )
	{
		if( it->second.m_typeId & asTYPEID_MASK_OBJECT )
			engine->GCEnumCallback(it->second.m_valueObj);
	}
}

void CScriptDictionary::ReleaseAllReferences(asIScriptEngine * /*engine*/)
{
	// We're being told to release all references in 
	// order to break circular references for dead objects
	DeleteAll();
}

CScriptDictionary &CScriptDictionary::operator =(const CScriptDictionary &other)
{
	// Clear everything we had before
	DeleteAll();

	// Do a shallow copy of the dictionary
	map<string, CScriptDictValue>::const_iterator it;
	for( it = other.dict.begin(); it != other.dict.end(); it++ )
	{
		if( it->second.m_typeId & asTYPEID_OBJHANDLE )
			Set(it->first, (void*)&it->second.m_valueObj, it->second.m_typeId);
		else if( it->second.m_typeId & asTYPEID_MASK_OBJECT )
			Set(it->first, (void*)it->second.m_valueObj, it->second.m_typeId);
		else
			Set(it->first, (void*)&it->second.m_valueInt, it->second.m_typeId);
	}

	return *this;
}

CScriptDictValue *CScriptDictionary::operator[](const string &key)
{
	// Return the existing value if it exists, else insert an empty value
	map<string, CScriptDictValue>::iterator it;
	it = dict.find(key);
	if( it == dict.end() )
		it = dict.insert(map<string, CScriptDictValue>::value_type(key, CScriptDictValue())).first;
	
	return &it->second;
}

const CScriptDictValue *CScriptDictionary::operator[](const string &key) const
{
	// Return the existing value if it exists
	map<string, CScriptDictValue>::const_iterator it;
	it = dict.find(key);
	if( it != dict.end() )
		return &it->second;

	// Else raise an exception
	asIScriptContext *ctx = asGetActiveContext();
	if( ctx )
		ctx->SetException("Invalid access to non-existing value");

	return 0;
}

void CScriptDictionary::Set(const string &key, void *value, int typeId)
{
	map<string, CScriptDictValue>::iterator it;
	it = dict.find(key);
	if( it == dict.end() )
		it = dict.insert(map<string, CScriptDictValue>::value_type(key, CScriptDictValue())).first;

	it->second.Set(engine, value, typeId);
}

// This overloaded method is implemented so that all integer and
// unsigned integers types will be stored in the dictionary as int64
// through implicit conversions. This simplifies the management of the
// numeric types when the script retrieves the stored value using a 
// different type.
void CScriptDictionary::Set(const string &key, const asINT64 &value)
{
	Set(key, const_cast<asINT64*>(&value), asTYPEID_INT64);
}

// This overloaded method is implemented so that all floating point types 
// will be stored in the dictionary as double through implicit conversions. 
// This simplifies the management of the numeric types when the script 
// retrieves the stored value using a different type.
void CScriptDictionary::Set(const string &key, const double &value)
{
	Set(key, const_cast<double*>(&value), asTYPEID_DOUBLE);
}

// Returns true if the value was successfully retrieved
bool CScriptDictionary::Get(const string &key, void *value, int typeId) const
{
	map<string, CScriptDictValue>::const_iterator it;
	it = dict.find(key);
	if( it != dict.end() )
		return it->second.Get(engine, value, typeId);

	// AngelScript has already initialized the value with a default value,
	// so we don't have to do anything if we don't find the element, or if 
	// the element is incompatible with the requested type.

	return false;
}

// Returns the type id of the stored value
int CScriptDictionary::GetTypeId(const string &key) const
{
	map<string, CScriptDictValue>::const_iterator it;
	it = dict.find(key);
	if( it != dict.end() )
		return it->second.m_typeId;

	return -1;
}

bool CScriptDictionary::Get(const string &key, asINT64 &value) const
{
	return Get(key, &value, asTYPEID_INT64);
}

bool CScriptDictionary::Get(const string &key, double &value) const
{
	return Get(key, &value, asTYPEID_DOUBLE);
}

bool CScriptDictionary::Exists(const string &key) const
{
	map<string, CScriptDictValue>::const_iterator it;
	it = dict.find(key);
	if( it != dict.end() )
		return true;

	return false;
}

bool CScriptDictionary::IsEmpty() const
{
	if( dict.size() == 0 )
		return true;

	return false;
}

asUINT CScriptDictionary::GetSize() const
{
	return asUINT(dict.size());
}

void CScriptDictionary::Delete(const string &key)
{
	map<string, CScriptDictValue>::iterator it;
	it = dict.find(key);
	if( it != dict.end() )
	{
		it->second.FreeValue(engine);
		dict.erase(it);
	}
}

void CScriptDictionary::DeleteAll()
{
	map<string, CScriptDictValue>::iterator it;
	for( it = dict.begin(); it != dict.end(); it++ )
		it->second.FreeValue(engine);

	dict.clear();
}

CScriptArray* CScriptDictionary::GetKeys() const
{
	// TODO: optimize: The string array type should only be determined once. 
	//                 It should be recomputed when registering the dictionary class.
	//                 Only problem is if multiple engines are used, as they may not
	//                 share the same type id. Alternatively it can be stored in the 
	//                 user data for the dictionary type.
	asIObjectType *ot = engine->GetObjectTypeByDecl("array<string>");

	// Create the array object
	CScriptArray *array = CScriptArray::Create(ot, asUINT(dict.size()));
	long current = -1;
	std::map<string, CScriptDictValue>::const_iterator it;
	for( it = dict.begin(); it != dict.end(); it++ )
	{
		current++;
		*(string*)array->At(current) = it->first;
	}

	return array;
}

//--------------------------------------------------------------------------
// Generic wrappers

void ScriptDictionaryFactory_Generic(asIScriptGeneric *gen)
{
	*(CScriptDictionary**)gen->GetAddressOfReturnLocation() = CScriptDictionary::Create(gen->GetEngine());
}

void ScriptDictionaryListFactory_Generic(asIScriptGeneric *gen)
{
	asBYTE *buffer = (asBYTE*)gen->GetArgAddress(0);
	*(CScriptDictionary**)gen->GetAddressOfReturnLocation() = CScriptDictionary::Create(buffer);
}

void ScriptDictionaryAddRef_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *dict = (CScriptDictionary*)gen->GetObject();
	dict->AddRef();
}

void ScriptDictionaryRelease_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *dict = (CScriptDictionary*)gen->GetObject();
	dict->Release();
}

void ScriptDictionaryAssign_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *dict = (CScriptDictionary*)gen->GetObject();
	CScriptDictionary *other = *(CScriptDictionary**)gen->GetAddressOfArg(0);
	*dict = *other;
	*(CScriptDictionary**)gen->GetAddressOfReturnLocation() = dict;
}

void ScriptDictionarySet_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *dict = (CScriptDictionary*)gen->GetObject();
	string *key = *(string**)gen->GetAddressOfArg(0);
	void *ref = *(void**)gen->GetAddressOfArg(1);
	int typeId = gen->GetArgTypeId(1);
	dict->Set(*key, ref, typeId);
}

void ScriptDictionarySetInt_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *dict = (CScriptDictionary*)gen->GetObject();
	string *key = *(string**)gen->GetAddressOfArg(0);
	void *ref = *(void**)gen->GetAddressOfArg(1);
	dict->Set(*key, *(asINT64*)ref);
}

void ScriptDictionarySetFlt_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *dict = (CScriptDictionary*)gen->GetObject();
	string *key = *(string**)gen->GetAddressOfArg(0);
	void *ref = *(void**)gen->GetAddressOfArg(1);
	dict->Set(*key, *(double*)ref);
}

void ScriptDictionaryGet_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *dict = (CScriptDictionary*)gen->GetObject();
	string *key = *(string**)gen->GetAddressOfArg(0);
	void *ref = *(void**)gen->GetAddressOfArg(1);
	int typeId = gen->GetArgTypeId(1);
	*(bool*)gen->GetAddressOfReturnLocation() = dict->Get(*key, ref, typeId);
}

void ScriptDictionaryGetInt_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *dict = (CScriptDictionary*)gen->GetObject();
	string *key = *(string**)gen->GetAddressOfArg(0);
	void *ref = *(void**)gen->GetAddressOfArg(1);
	*(bool*)gen->GetAddressOfReturnLocation() = dict->Get(*key, *(asINT64*)ref);
}

void ScriptDictionaryGetFlt_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *dict = (CScriptDictionary*)gen->GetObject();
	string *key = *(string**)gen->GetAddressOfArg(0);
	void *ref = *(void**)gen->GetAddressOfArg(1);
	*(bool*)gen->GetAddressOfReturnLocation() = dict->Get(*key, *(double*)ref);
}

void ScriptDictionaryExists_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *dict = (CScriptDictionary*)gen->GetObject();
	string *key = *(string**)gen->GetAddressOfArg(0);
	bool ret = dict->Exists(*key);
	*(bool*)gen->GetAddressOfReturnLocation() = ret;
}

void ScriptDictionaryIsEmpty_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *dict = (CScriptDictionary*)gen->GetObject();
	bool ret = dict->IsEmpty();
	*(bool*)gen->GetAddressOfReturnLocation() = ret;
}

void ScriptDictionaryGetSize_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *dict = (CScriptDictionary*)gen->GetObject();
	asUINT ret = dict->GetSize();
	*(asUINT*)gen->GetAddressOfReturnLocation() = ret;
}

void ScriptDictionaryDelete_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *dict = (CScriptDictionary*)gen->GetObject();
	string *key = *(string**)gen->GetAddressOfArg(0);
	dict->Delete(*key);
}

void ScriptDictionaryDeleteAll_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *dict = (CScriptDictionary*)gen->GetObject();
	dict->DeleteAll();
}

static void ScriptDictionaryGetRefCount_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *self = (CScriptDictionary*)gen->GetObject();
	*(int*)gen->GetAddressOfReturnLocation() = self->GetRefCount();
}

static void ScriptDictionarySetGCFlag_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *self = (CScriptDictionary*)gen->GetObject();
	self->SetGCFlag();
}

static void ScriptDictionaryGetGCFlag_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *self = (CScriptDictionary*)gen->GetObject();
	*(bool*)gen->GetAddressOfReturnLocation() = self->GetGCFlag();
}

static void ScriptDictionaryEnumReferences_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *self = (CScriptDictionary*)gen->GetObject();
	asIScriptEngine *engine = *(asIScriptEngine**)gen->GetAddressOfArg(0);
	self->EnumReferences(engine);
}

static void ScriptDictionaryReleaseAllReferences_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *self = (CScriptDictionary*)gen->GetObject();
	asIScriptEngine *engine = *(asIScriptEngine**)gen->GetAddressOfArg(0);
	self->ReleaseAllReferences(engine);
}

static void CScriptDictionaryGetKeys_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *self = (CScriptDictionary*)gen->GetObject();
	*(CScriptArray**)gen->GetAddressOfReturnLocation() = self->GetKeys();
}

static void CScriptDictionary_opIndex_Generic(asIScriptGeneric *gen)
{
	CScriptDictionary *self = (CScriptDictionary*)gen->GetObject();
	std::string *key = (std::string*)gen->GetAddressOfArg(0);
	*(CScriptDictValue**)gen->GetAddressOfReturnLocation() = self->operator[](*key);
}

static void CScriptDictionary_opIndex_const_Generic(asIScriptGeneric *gen)
{
	const CScriptDictionary *self = (const CScriptDictionary*)gen->GetObject();
	std::string *key = (std::string*)gen->GetAddressOfArg(0);
	*(const CScriptDictValue**)gen->GetAddressOfReturnLocation() = self->operator[](*key);
}


//-------------------------------------------------------------------------
// CScriptDictValue

CScriptDictValue::CScriptDictValue()
{
	m_valueObj = 0;
	m_typeId   = 0;
}

CScriptDictValue::CScriptDictValue(asIScriptEngine *engine, void *value, int typeId)
{
	m_valueObj = 0;
	m_typeId   = 0;
	Set(engine, value, typeId);
}

CScriptDictValue::~CScriptDictValue()
{
	// Must not hold an object when destroyed, as then the object will never be freed
	assert( (m_typeId & asTYPEID_MASK_OBJECT) == 0 );
}

void CScriptDictValue::FreeValue(asIScriptEngine *engine)
{
	// If it is a handle or a ref counted object, call release
	if( m_typeId & asTYPEID_MASK_OBJECT )
	{
		// Let the engine release the object
		engine->ReleaseScriptObject(m_valueObj, engine->GetObjectTypeById(m_typeId));
		m_valueObj = 0;
		m_typeId = 0;
	}

	// For primitives, there's nothing to do
}

void CScriptDictValue::Set(asIScriptEngine *engine, void *value, int typeId)
{
	FreeValue(engine);

	m_typeId = typeId;
	if( typeId & asTYPEID_OBJHANDLE )
	{
		// We're receiving a reference to the handle, so we need to dereference it
		m_valueObj = *(void**)value;
		engine->AddRefScriptObject(m_valueObj, engine->GetObjectTypeById(typeId));
	}
	else if( typeId & asTYPEID_MASK_OBJECT )
	{
		// Create a copy of the object
		m_valueObj = engine->CreateScriptObjectCopy(value, engine->GetObjectTypeById(typeId));
	}
	else
	{
		// Copy the primitive value
		// We receive a pointer to the value.
		int size = engine->GetSizeOfPrimitiveType(typeId);
		memcpy(&m_valueInt, value, size);
	}
}

// This overloaded method is implemented so that all integer and
// unsigned integers types will be stored in the dictionary as int64
// through implicit conversions. This simplifies the management of the
// numeric types when the script retrieves the stored value using a 
// different type.
void CScriptDictValue::Set(asIScriptEngine *engine, const asINT64 &value)
{
	Set(engine, const_cast<asINT64*>(&value), asTYPEID_INT64);
}

// This overloaded method is implemented so that all floating point types 
// will be stored in the dictionary as double through implicit conversions. 
// This simplifies the management of the numeric types when the script 
// retrieves the stored value using a different type.
void CScriptDictValue::Set(asIScriptEngine *engine, const double &value)
{
	Set(engine, const_cast<double*>(&value), asTYPEID_DOUBLE);
}

bool CScriptDictValue::Get(asIScriptEngine *engine, void *value, int typeId) const
{
	// Return the value
	if( typeId & asTYPEID_OBJHANDLE )
	{
		// A handle can be retrieved if the stored type is a handle of same or compatible type
		// or if the stored type is an object that implements the interface that the handle refer to.
		if( (m_typeId & asTYPEID_MASK_OBJECT) && 
			engine->IsHandleCompatibleWithObject(m_valueObj, m_typeId, typeId) )
		{
			engine->AddRefScriptObject(m_valueObj, engine->GetObjectTypeById(m_typeId));
			*(void**)value = m_valueObj;

			return true;
		}
	}
	else if( typeId & asTYPEID_MASK_OBJECT )
	{
		// Verify that the copy can be made
		bool isCompatible = false;

		// Allow a handle to be value assigned if the wanted type is not a handle
		if( (m_typeId & ~asTYPEID_OBJHANDLE) == typeId && m_valueObj != 0 )
			isCompatible = true;

		// Copy the object into the given reference
		if( isCompatible )
		{
			engine->AssignScriptObject(value, m_valueObj, engine->GetObjectTypeById(typeId));

			return true;
		}
	}
	else
	{
		if( m_typeId == typeId )
		{
			int size = engine->GetSizeOfPrimitiveType(typeId);
			memcpy(value, &m_valueInt, size);
			return true;
		}

		// We know all numbers are stored as either int64 or double, since we register overloaded functions for those
		if( typeId == asTYPEID_DOUBLE )
		{
			if( m_typeId == asTYPEID_INT64 )
				*(double*)value = double(m_valueInt);
			else if( m_typeId == asTYPEID_BOOL )
				*(double*)value = char(m_valueInt) ? 1.0 : 0.0;
			else if( m_typeId > asTYPEID_DOUBLE && (m_typeId & asTYPEID_MASK_OBJECT) == 0 )
				*(double*)value = double(int(m_valueInt)); // enums are 32bit
			else
			{
				// The stored type is an object
				// TODO: Check if the object has a conversion operator to a primitive value
				*(double*)value = 0;
			}
			return true;
		}
		else if( typeId == asTYPEID_INT64 )
		{
			if( m_typeId == asTYPEID_DOUBLE )
				*(asINT64*)value = asINT64(m_valueFlt);
			else if( m_typeId == asTYPEID_BOOL )
				*(asINT64*)value = char(m_valueInt) ? 1 : 0;
			else if( m_typeId > asTYPEID_DOUBLE && (m_typeId & asTYPEID_MASK_OBJECT) == 0 )
				*(asINT64*)value = int(m_valueInt); // enums are 32bit
			else
			{
				// The stored type is an object
				// TODO: Check if the object has a conversion operator to a primitive value
				*(asINT64*)value = 0;
			}
			return true;
		}
		else if( typeId > asTYPEID_DOUBLE && (m_typeId & asTYPEID_MASK_OBJECT) == 0 )
		{
			// The desired type is an enum. These are always 32bit integers
			if( m_typeId == asTYPEID_DOUBLE )
				*(int*)value = int(m_valueFlt);
			else if( m_typeId == asTYPEID_INT64 )
				*(int*)value = int(m_valueInt);
			else if( m_typeId == asTYPEID_BOOL )
				*(int*)value = char(m_valueInt) ? 1 : 0;
			else if( m_typeId > asTYPEID_DOUBLE && (m_typeId & asTYPEID_MASK_OBJECT) == 0 )
				*(int*)value = int(m_valueInt);
			else
			{
				// The stored type is an object
				// TODO: Check if the object has a conversion operator to a primitive value
				*(int*)value = 0;
			}
		}
	}

	// It was not possible to retrieve the value using the desired typeId
	return false;
}

bool CScriptDictValue::Get(asIScriptEngine *engine, asINT64 &value) const
{
	return Get(engine, &value, asTYPEID_INT64);
}

bool CScriptDictValue::Get(asIScriptEngine *engine, double &value) const
{
	return Get(engine, &value, asTYPEID_DOUBLE);
}

int CScriptDictValue::GetTypeId() const
{
	return m_typeId;
}

static void CScriptDictValue_Construct(void *mem)
{
	new(mem) CScriptDictValue();
}

static void CScriptDictValue_Destruct(CScriptDictValue *obj)
{
	asIScriptContext *ctx = asGetActiveContext();
	if( ctx )
	{
		asIScriptEngine *engine = ctx->GetEngine();
		obj->FreeValue(engine);
	}
	obj->~CScriptDictValue();
}

static CScriptDictValue &CScriptDictValue_opAssign(void *ref, int typeId, CScriptDictValue *obj)
{
	asIScriptContext *ctx = asGetActiveContext();
	if( ctx )
	{
		asIScriptEngine *engine = ctx->GetEngine();
		obj->Set(engine, ref, typeId);
	}
	return *obj;
}

static CScriptDictValue &CScriptDictValue_opAssign(double val, CScriptDictValue *obj)
{
	return CScriptDictValue_opAssign(&val, asTYPEID_DOUBLE, obj);
}

static CScriptDictValue &CScriptDictValue_opAssign(asINT64 val, CScriptDictValue *obj)
{
	return CScriptDictValue_opAssign(&val, asTYPEID_INT64, obj);
}

static void CScriptDictValue_opCast(void *ref, int typeId, CScriptDictValue *obj)
{
	asIScriptContext *ctx = asGetActiveContext();
	if( ctx )
	{
		asIScriptEngine *engine = ctx->GetEngine();
		obj->Get(engine, ref, typeId);
	}
}

static asINT64 CScriptDictValue_opConvInt(CScriptDictValue *obj)
{
	asINT64 value;
	CScriptDictValue_opCast(&value, asTYPEID_INT64, obj);
	return value;
}

static double CScriptDictValue_opConvDouble(CScriptDictValue *obj)
{
	double value;
	CScriptDictValue_opCast(&value, asTYPEID_DOUBLE, obj);
	return value;
}

//-------------------------------------------------------------------
// generic wrapper for CScriptDictValue

static void CScriptDictValue_opConvDouble_Generic(asIScriptGeneric *gen)
{
	CScriptDictValue *self = (CScriptDictValue*)gen->GetObject();
	double value;
	self->Get(gen->GetEngine(), value);
	*(double*)gen->GetAddressOfReturnLocation() = value;
}

static void CScriptDictValue_opConvInt_Generic(asIScriptGeneric *gen)
{
	CScriptDictValue *self = (CScriptDictValue*)gen->GetObject();
	asINT64 value;
	self->Get(gen->GetEngine(), value);
	*(asINT64*)gen->GetAddressOfReturnLocation() = value;
}

static void CScriptDictValue_opCast_Generic(asIScriptGeneric *gen)
{
	CScriptDictValue *self = (CScriptDictValue*)gen->GetObject();
	self->Get(gen->GetEngine(), gen->GetArgAddress(0), gen->GetArgTypeId(0));
}

static void CScriptDictValue_opAssign_int64_Generic(asIScriptGeneric *gen)
{
	CScriptDictValue *self = (CScriptDictValue*)gen->GetObject();
	*(CScriptDictValue**)gen->GetAddressOfReturnLocation() = &CScriptDictValue_opAssign((asINT64)gen->GetArgQWord(0), self);
}

static void CScriptDictValue_opAssign_double_Generic(asIScriptGeneric *gen)
{
	CScriptDictValue *self = (CScriptDictValue*)gen->GetObject();
	*(CScriptDictValue**)gen->GetAddressOfReturnLocation() = &CScriptDictValue_opAssign(gen->GetArgDouble(0), self);
}

static void CScriptDictValue_opAssign_Generic(asIScriptGeneric *gen)
{
	CScriptDictValue *self = (CScriptDictValue*)gen->GetObject();
	*(CScriptDictValue**)gen->GetAddressOfReturnLocation() = &CScriptDictValue_opAssign(gen->GetArgAddress(0), gen->GetArgTypeId(0), self);
}

static void CScriptDictValue_Construct_Generic(asIScriptGeneric *gen)
{
	CScriptDictValue *self = (CScriptDictValue*)gen->GetObject();
	CScriptDictValue_Construct(self);
}

static void CScriptDictValue_Destruct_Generic(asIScriptGeneric *gen)
{
	CScriptDictValue *self = (CScriptDictValue*)gen->GetObject();
	CScriptDictValue_Destruct(self);
}

//--------------------------------------------------------------------------
// Register the type

void RegisterScriptDictionary(asIScriptEngine *engine)
{
	if( strstr(asGetLibraryOptions(), "AS_MAX_PORTABILITY") )
		RegisterScriptDictionary_Generic(engine);
	else
		RegisterScriptDictionary_Native(engine);
}

void RegisterScriptDictionary_Native(asIScriptEngine *engine)
{
	int r;

	r = engine->RegisterObjectType("dictionaryValue", sizeof(CScriptDictValue), asOBJ_VALUE | asOBJ_ASHANDLE | asOBJ_APP_CLASS_CD); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionaryValue", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(CScriptDictValue_Construct), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionaryValue", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(CScriptDictValue_Destruct), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionaryValue", "dictionaryValue &opHndlAssign(const ?&in)", asFUNCTIONPR(CScriptDictValue_opAssign, (void *, int, CScriptDictValue*), CScriptDictValue &), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionaryValue", "dictionaryValue &opAssign(const ?&in)", asFUNCTIONPR(CScriptDictValue_opAssign, (void *, int, CScriptDictValue*), CScriptDictValue &), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionaryValue", "dictionaryValue &opAssign(double)", asFUNCTIONPR(CScriptDictValue_opAssign, (double, CScriptDictValue*), CScriptDictValue &), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionaryValue", "dictionaryValue &opAssign(int64)", asFUNCTIONPR(CScriptDictValue_opAssign, (asINT64, CScriptDictValue*), CScriptDictValue &), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionaryValue", asBEHAVE_REF_CAST, "void opCast(?&out)", asFUNCTIONPR(CScriptDictValue_opCast, (void *, int, CScriptDictValue*), void), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionaryValue", asBEHAVE_VALUE_CAST, "void opConv(?&out)", asFUNCTIONPR(CScriptDictValue_opCast, (void *, int, CScriptDictValue*), void), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionaryValue", asBEHAVE_VALUE_CAST, "int64 opConv()", asFUNCTIONPR(CScriptDictValue_opConvInt, (CScriptDictValue*), asINT64), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionaryValue", asBEHAVE_VALUE_CAST, "double opConv()", asFUNCTIONPR(CScriptDictValue_opConvDouble, (CScriptDictValue*), double), asCALL_CDECL_OBJLAST); assert( r >= 0 );
	
	r = engine->RegisterObjectType("dictionary", sizeof(CScriptDictionary), asOBJ_REF | asOBJ_GC); assert( r >= 0 );
	// Use the generic interface to construct the object since we need the engine pointer, we could also have retrieved the engine pointer from the active context
	r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_FACTORY, "dictionary@ f()", asFUNCTION(ScriptDictionaryFactory_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_LIST_FACTORY, "dictionary @f(int &in) {repeat {string, ?}}", asFUNCTION(ScriptDictionaryListFactory_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_ADDREF, "void f()", asMETHOD(CScriptDictionary,AddRef), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_RELEASE, "void f()", asMETHOD(CScriptDictionary,Release), asCALL_THISCALL); assert( r >= 0 );

	r = engine->RegisterObjectMethod("dictionary", "dictionary &opAssign(const dictionary &in)", asMETHODPR(CScriptDictionary, operator=, (const CScriptDictionary &), CScriptDictionary&), asCALL_THISCALL); assert( r >= 0 );

	r = engine->RegisterObjectMethod("dictionary", "void set(const string &in, const ?&in)", asMETHODPR(CScriptDictionary,Set,(const string&,void*,int),void), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionary", "bool get(const string &in, ?&out) const", asMETHODPR(CScriptDictionary,Get,(const string&,void*,int) const,bool), asCALL_THISCALL); assert( r >= 0 );

	r = engine->RegisterObjectMethod("dictionary", "void set(const string &in, const int64&in)", asMETHODPR(CScriptDictionary,Set,(const string&,const asINT64&),void), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionary", "bool get(const string &in, int64&out) const", asMETHODPR(CScriptDictionary,Get,(const string&,asINT64&) const,bool), asCALL_THISCALL); assert( r >= 0 );

	r = engine->RegisterObjectMethod("dictionary", "void set(const string &in, const double&in)", asMETHODPR(CScriptDictionary,Set,(const string&,const double&),void), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionary", "bool get(const string &in, double&out) const", asMETHODPR(CScriptDictionary,Get,(const string&,double&) const,bool), asCALL_THISCALL); assert( r >= 0 );

	r = engine->RegisterObjectMethod("dictionary", "bool exists(const string &in) const", asMETHOD(CScriptDictionary,Exists), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionary", "bool isEmpty() const", asMETHOD(CScriptDictionary, IsEmpty), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionary", "uint getSize() const", asMETHOD(CScriptDictionary, GetSize), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionary", "void delete(const string &in)", asMETHOD(CScriptDictionary,Delete), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionary", "void deleteAll()", asMETHOD(CScriptDictionary,DeleteAll), asCALL_THISCALL); assert( r >= 0 );

	r = engine->RegisterObjectMethod("dictionary", "array<string> @getKeys() const", asMETHOD(CScriptDictionary,GetKeys), asCALL_THISCALL); assert( r >= 0 );

	r = engine->RegisterObjectMethod("dictionary", "dictionaryValue &opIndex(const string &in)", asMETHODPR(CScriptDictionary, operator[], (const string &), CScriptDictValue*), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionary", "const dictionaryValue &opIndex(const string &in) const", asMETHODPR(CScriptDictionary, operator[], (const string &) const, const CScriptDictValue*), asCALL_THISCALL); assert( r >= 0 );

	// Register GC behaviours
	r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_GETREFCOUNT, "int f()", asMETHOD(CScriptDictionary,GetRefCount), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_SETGCFLAG, "void f()", asMETHOD(CScriptDictionary,SetGCFlag), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_GETGCFLAG, "bool f()", asMETHOD(CScriptDictionary,GetGCFlag), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_ENUMREFS, "void f(int&in)", asMETHOD(CScriptDictionary,EnumReferences), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_RELEASEREFS, "void f(int&in)", asMETHOD(CScriptDictionary,ReleaseAllReferences), asCALL_THISCALL); assert( r >= 0 );

#if AS_USE_STLNAMES == 1
	// Same as isEmpty
	r = engine->RegisterObjectMethod("dictionary", "bool empty() const", asMETHOD(CScriptDictionary, IsEmpty), asCALL_THISCALL); assert( r >= 0 );
	// Same as getSize
	r = engine->RegisterObjectMethod("dictionary", "uint size() const", asMETHOD(CScriptDictionary, GetSize), asCALL_THISCALL); assert( r >= 0 );
	// Same as delete
	r = engine->RegisterObjectMethod("dictionary", "void erase(const string &in)", asMETHOD(CScriptDictionary,Delete), asCALL_THISCALL); assert( r >= 0 );
	// Same as deleteAll
	r = engine->RegisterObjectMethod("dictionary", "void clear()", asMETHOD(CScriptDictionary,DeleteAll), asCALL_THISCALL); assert( r >= 0 );
#endif
}

void RegisterScriptDictionary_Generic(asIScriptEngine *engine)
{
	int r;

	r = engine->RegisterObjectType("dictionaryValue", sizeof(CScriptDictValue), asOBJ_VALUE | asOBJ_ASHANDLE | asOBJ_APP_CLASS_CD); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionaryValue", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(CScriptDictValue_Construct_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionaryValue", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(CScriptDictValue_Destruct_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionaryValue", "dictionaryValue &opHndlAssign(const ?&in)", asFUNCTION(CScriptDictValue_opAssign_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionaryValue", "dictionaryValue &opAssign(const ?&in)", asFUNCTION(CScriptDictValue_opAssign_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionaryValue", "dictionaryValue &opAssign(double)", asFUNCTION(CScriptDictValue_opAssign_double_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionaryValue", "dictionaryValue &opAssign(int64)", asFUNCTION(CScriptDictValue_opAssign_int64_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionaryValue", asBEHAVE_REF_CAST, "void opCast(?&out)", asFUNCTION(CScriptDictValue_opCast_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionaryValue", asBEHAVE_VALUE_CAST, "void opConv(?&out)", asFUNCTION(CScriptDictValue_opCast_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionaryValue", asBEHAVE_VALUE_CAST, "int64 opConv()", asFUNCTION(CScriptDictValue_opConvInt_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionaryValue", asBEHAVE_VALUE_CAST, "double opConv()", asFUNCTION(CScriptDictValue_opConvDouble_Generic), asCALL_GENERIC); assert( r >= 0 );

	r = engine->RegisterObjectType("dictionary", sizeof(CScriptDictionary), asOBJ_REF | asOBJ_GC); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_FACTORY, "dictionary@ f()", asFUNCTION(ScriptDictionaryFactory_Generic), asCALL_GENERIC); assert( r>= 0 );
	r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_LIST_FACTORY, "dictionary @f(int &in) {repeat {string, ?}}", asFUNCTION(ScriptDictionaryListFactory_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_ADDREF, "void f()", asFUNCTION(ScriptDictionaryAddRef_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_RELEASE, "void f()", asFUNCTION(ScriptDictionaryRelease_Generic), asCALL_GENERIC); assert( r >= 0 );

	r = engine->RegisterObjectMethod("dictionary", "dictionary &opAssign(const dictionary &in)", asFUNCTION(ScriptDictionaryAssign_Generic), asCALL_GENERIC); assert( r >= 0 );

	r = engine->RegisterObjectMethod("dictionary", "void set(const string &in, const ?&in)", asFUNCTION(ScriptDictionarySet_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionary", "bool get(const string &in, ?&out) const", asFUNCTION(ScriptDictionaryGet_Generic), asCALL_GENERIC); assert( r >= 0 );

	r = engine->RegisterObjectMethod("dictionary", "void set(const string &in, const int64&in)", asFUNCTION(ScriptDictionarySetInt_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionary", "bool get(const string &in, int64&out) const", asFUNCTION(ScriptDictionaryGetInt_Generic), asCALL_GENERIC); assert( r >= 0 );

	r = engine->RegisterObjectMethod("dictionary", "void set(const string &in, const double&in)", asFUNCTION(ScriptDictionarySetFlt_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionary", "bool get(const string &in, double&out) const", asFUNCTION(ScriptDictionaryGetFlt_Generic), asCALL_GENERIC); assert( r >= 0 );

	r = engine->RegisterObjectMethod("dictionary", "bool exists(const string &in) const", asFUNCTION(ScriptDictionaryExists_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionary", "bool isEmpty() const", asFUNCTION(ScriptDictionaryIsEmpty_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionary", "uint getSize() const", asFUNCTION(ScriptDictionaryGetSize_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionary", "void delete(const string &in)", asFUNCTION(ScriptDictionaryDelete_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionary", "void deleteAll()", asFUNCTION(ScriptDictionaryDeleteAll_Generic), asCALL_GENERIC); assert( r >= 0 );

	r = engine->RegisterObjectMethod("dictionary", "array<string> @getKeys() const", asFUNCTION(CScriptDictionaryGetKeys_Generic), asCALL_GENERIC); assert( r >= 0 );

	r = engine->RegisterObjectMethod("dictionary", "dictionaryValue &opIndex(const string &in)", asFUNCTION(CScriptDictionary_opIndex_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectMethod("dictionary", "const dictionaryValue &opIndex(const string &in) const", asFUNCTION(CScriptDictionary_opIndex_const_Generic), asCALL_GENERIC); assert( r >= 0 );

	// Register GC behaviours
	r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_GETREFCOUNT, "int f()", asFUNCTION(ScriptDictionaryGetRefCount_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_SETGCFLAG, "void f()", asFUNCTION(ScriptDictionarySetGCFlag_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_GETGCFLAG, "bool f()", asFUNCTION(ScriptDictionaryGetGCFlag_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_ENUMREFS, "void f(int&in)", asFUNCTION(ScriptDictionaryEnumReferences_Generic), asCALL_GENERIC); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_RELEASEREFS, "void f(int&in)", asFUNCTION(ScriptDictionaryReleaseAllReferences_Generic), asCALL_GENERIC); assert( r >= 0 );
}

//------------------------------------------------------------------
// Iterator implementation

CScriptDictionary::CIterator CScriptDictionary::begin() const
{
	return CIterator(*this, dict.begin());
}

CScriptDictionary::CIterator CScriptDictionary::end() const
{
	return CIterator(*this, dict.end());
}

CScriptDictionary::CIterator::CIterator(
		const CScriptDictionary &dict,
		std::map<std::string, CScriptDictValue>::const_iterator it)
	: m_it(it), m_dict(dict)
{}

void CScriptDictionary::CIterator::operator++() 
{ 
	++m_it; 
}

void CScriptDictionary::CIterator::operator++(int) 
{ 
	++m_it;

	// Normally the post increment would return a copy of the object with the original state,
	// but it is rarely used so we skip this extra copy to avoid unnecessary overhead
}

CScriptDictionary::CIterator &CScriptDictionary::CIterator::operator*()
{
	return *this;
}

bool CScriptDictionary::CIterator::operator==(const CIterator &other) const 
{ 
	return m_it == other.m_it;
}

bool CScriptDictionary::CIterator::operator!=(const CIterator &other) const 
{ 
	return m_it != other.m_it; 
}

const std::string &CScriptDictionary::CIterator::GetKey() const 
{ 
	return m_it->first; 
}

int CScriptDictionary::CIterator::GetTypeId() const
{ 
	return m_it->second.m_typeId; 
}

bool CScriptDictionary::CIterator::GetValue(asINT64 &value) const
{ 
	return m_it->second.Get(m_dict.engine, &value, asTYPEID_INT64); 
}

bool CScriptDictionary::CIterator::GetValue(double &value) const
{ 
	return m_it->second.Get(m_dict.engine, &value, asTYPEID_DOUBLE); 
}

bool CScriptDictionary::CIterator::GetValue(void *value, int typeId) const
{ 
	return m_it->second.Get(m_dict.engine, value, typeId); 
}

END_AS_NAMESPACE


