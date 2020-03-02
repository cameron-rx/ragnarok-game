
#include "main.h"

//${CONFIG_BEGIN}
#define CFG_BINARY_FILES *.bin|*.dat
#define CFG_BRL_GAMETARGET_IMPLEMENTED 1
#define CFG_BRL_THREAD_IMPLEMENTED 1
#define CFG_CONFIG debug
#define CFG_CPP_GC_MODE 1
#define CFG_GLFW_SWAP_INTERVAL -1
#define CFG_GLFW_USE_MINGW 1
#define CFG_GLFW_WINDOW_FULLSCREEN 0
#define CFG_GLFW_WINDOW_HEIGHT 480
#define CFG_GLFW_WINDOW_RESIZABLE 0
#define CFG_GLFW_WINDOW_TITLE Monkey Game
#define CFG_GLFW_WINDOW_WIDTH 640
#define CFG_HOST winnt
#define CFG_IMAGE_FILES *.png|*.jpg
#define CFG_LANG cpp
#define CFG_MOJO_AUTO_SUSPEND_ENABLED 1
#define CFG_MOJO_DRIVER_IMPLEMENTED 1
#define CFG_MOJO_IMAGE_FILTERING_ENABLED 1
#define CFG_MUSIC_FILES *.wav|*.ogg
#define CFG_OPENGL_DEPTH_BUFFER_ENABLED 0
#define CFG_OPENGL_GLES20_ENABLED 0
#define CFG_SAFEMODE 0
#define CFG_SOUND_FILES *.wav|*.ogg
#define CFG_TARGET glfw
#define CFG_TEXT_FILES *.txt|*.xml|*.json
//${CONFIG_END}

//${TRANSCODE_BEGIN}

#include <wctype.h>
#include <locale.h>

// C++ Monkey runtime.
//
// Placed into the public domain 24/02/2011.
// No warranty implied; use at your own risk.

//***** Monkey Types *****

typedef wchar_t Char;
template<class T> class Array;
class String;
class Object;

#if CFG_CPP_DOUBLE_PRECISION_FLOATS
typedef double Float;
#define FLOAT(X) X
#else
typedef float Float;
#define FLOAT(X) X##f
#endif

void dbg_error( const char *p );

#if !_MSC_VER
#define sprintf_s sprintf
#define sscanf_s sscanf
#endif

//***** GC Config *****

#define DEBUG_GC 0

// GC mode:
//
// 0 = disabled
// 1 = Incremental GC every OnWhatever
// 2 = Incremental GC every allocation
//
#ifndef CFG_CPP_GC_MODE
#define CFG_CPP_GC_MODE 1
#endif

//How many bytes alloced to trigger GC
//
#ifndef CFG_CPP_GC_TRIGGER
#define CFG_CPP_GC_TRIGGER 8*1024*1024
#endif

//GC_MODE 2 needs to track locals on a stack - this may need to be bumped if your app uses a LOT of locals, eg: is heavily recursive...
//
#ifndef CFG_CPP_GC_MAX_LOCALS
#define CFG_CPP_GC_MAX_LOCALS 8192
#endif

// ***** GC *****

#if _WIN32

int gc_micros(){
	static int f;
	static LARGE_INTEGER pcf;
	if( !f ){
		if( QueryPerformanceFrequency( &pcf ) && pcf.QuadPart>=1000000L ){
			pcf.QuadPart/=1000000L;
			f=1;
		}else{
			f=-1;
		}
	}
	if( f>0 ){
		LARGE_INTEGER pc;
		if( QueryPerformanceCounter( &pc ) ) return pc.QuadPart/pcf.QuadPart;
		f=-1;
	}
	return 0;// timeGetTime()*1000;
}

#elif __APPLE__

#include <mach/mach_time.h>

int gc_micros(){
	static int f;
	static mach_timebase_info_data_t timeInfo;
	if( !f ){
		mach_timebase_info( &timeInfo );
		timeInfo.denom*=1000L;
		f=1;
	}
	return mach_absolute_time()*timeInfo.numer/timeInfo.denom;
}

#else

int gc_micros(){
	return 0;
}

#endif

#define gc_mark_roots gc_mark

void gc_mark_roots();

struct gc_object;

gc_object *gc_object_alloc( int size );
void gc_object_free( gc_object *p );

struct gc_object{
	gc_object *succ;
	gc_object *pred;
	int flags;
	
	virtual ~gc_object(){
	}
	
	virtual void mark(){
	}
	
	void *operator new( size_t size ){
		return gc_object_alloc( size );
	}
	
	void operator delete( void *p ){
		gc_object_free( (gc_object*)p );
	}
};

gc_object gc_free_list;
gc_object gc_marked_list;
gc_object gc_unmarked_list;
gc_object gc_queued_list;	//doesn't really need to be doubly linked...

int gc_free_bytes;
int gc_marked_bytes;
int gc_alloced_bytes;
int gc_max_alloced_bytes;
int gc_new_bytes;
int gc_markbit=1;

gc_object *gc_cache[8];

int gc_ctor_nest;
gc_object *gc_locals[CFG_CPP_GC_MAX_LOCALS],**gc_locals_sp=gc_locals;

void gc_collect_all();
void gc_mark_queued( int n );

#define GC_CLEAR_LIST( LIST ) ((LIST).succ=(LIST).pred=&(LIST))

#define GC_LIST_IS_EMPTY( LIST ) ((LIST).succ==&(LIST))

#define GC_REMOVE_NODE( NODE ){\
(NODE)->pred->succ=(NODE)->succ;\
(NODE)->succ->pred=(NODE)->pred;}

#define GC_INSERT_NODE( NODE,SUCC ){\
(NODE)->pred=(SUCC)->pred;\
(NODE)->succ=(SUCC);\
(SUCC)->pred->succ=(NODE);\
(SUCC)->pred=(NODE);}

void gc_init1(){
	GC_CLEAR_LIST( gc_free_list );
	GC_CLEAR_LIST( gc_marked_list );
	GC_CLEAR_LIST( gc_unmarked_list);
	GC_CLEAR_LIST( gc_queued_list );
}

void gc_init2(){
	gc_mark_roots();
}

#if CFG_CPP_GC_MODE==2

struct gc_ctor{
	gc_ctor(){ ++gc_ctor_nest; }
	~gc_ctor(){ --gc_ctor_nest; }
};

struct gc_enter{
	gc_object **sp;
	gc_enter():sp(gc_locals_sp){
	}
	~gc_enter(){
	/*
		static int max_locals;
		int n=gc_locals_sp-gc_locals;
		if( n>max_locals ){
			max_locals=n;
			printf( "max_locals=%i\n",n );
		}
	*/
		gc_locals_sp=sp;
	}
};

#define GC_CTOR gc_ctor _c;
#define GC_ENTER gc_enter _e;

#else

struct gc_ctor{
};
struct gc_enter{
};

#define GC_CTOR
#define GC_ENTER

#endif

void gc_flush_free( int size ){

	int t=gc_free_bytes-size;
	if( t<0 ) t=0;
	
	while( gc_free_bytes>t ){
		gc_object *p=gc_free_list.succ;
		if( !p || p==&gc_free_list ){
//			printf("ERROR:p=%p gc_free_bytes=%i\n",p,gc_free_bytes);
//			fflush(stdout);
			gc_free_bytes=0;
			break;
		}
		GC_REMOVE_NODE(p);
		delete p;	//...to gc_free
	}
}

void *gc_ext_malloc( int size ){

	gc_new_bytes+=size;
	
	gc_flush_free( size );
	
	return malloc( size );
}

void gc_ext_malloced( int size ){

	gc_new_bytes+=size;
	
	gc_flush_free( size );
}

gc_object *gc_object_alloc( int size ){

	size=(size+7)&~7;
	
#if CFG_CPP_GC_MODE==1

	gc_new_bytes+=size;
	
#elif CFG_CPP_GC_MODE==2

	if( !gc_ctor_nest ){
#if DEBUG_GC
		int ms=gc_micros();
#endif
		if( gc_new_bytes+size>(CFG_CPP_GC_TRIGGER) ){
			gc_collect_all();
			gc_new_bytes=size;
		}else{
			gc_new_bytes+=size;
			gc_mark_queued( (long long)(gc_new_bytes)*(gc_alloced_bytes-gc_new_bytes)/(CFG_CPP_GC_TRIGGER)+gc_new_bytes );
		}
		
#if DEBUG_GC
		ms=gc_micros()-ms;
		if( ms>=100 ) {printf( "gc time:%i\n",ms );fflush( stdout );}
#endif
	}

#endif

	gc_flush_free( size );

	gc_object *p;
	if( size<64 && (p=gc_cache[size>>3]) ){
		gc_cache[size>>3]=p->succ;
	}else{
		p=(gc_object*)malloc( size );
	}
	
	p->flags=size|gc_markbit;
	GC_INSERT_NODE( p,&gc_unmarked_list );

	gc_alloced_bytes+=size;
	if( gc_alloced_bytes>gc_max_alloced_bytes ) gc_max_alloced_bytes=gc_alloced_bytes;
	
#if CFG_CPP_GC_MODE==2
	*gc_locals_sp++=p;
#endif

	return p;
}

void gc_object_free( gc_object *p ){

	int size=p->flags & ~7;
	gc_free_bytes-=size;
	
	if( size<64 ){
		p->succ=gc_cache[size>>3];
		gc_cache[size>>3]=p;
	}else{
		free( p );
	}
}

template<class T> void gc_mark( T *t ){

	gc_object *p=dynamic_cast<gc_object*>(t);
	
	if( p && (p->flags & 3)==gc_markbit ){
		p->flags^=1;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_marked_list );
		gc_marked_bytes+=(p->flags & ~7);
		p->mark();
	}
}

template<class T> void gc_mark_q( T *t ){

	gc_object *p=dynamic_cast<gc_object*>(t);
	
	if( p && (p->flags & 3)==gc_markbit ){
		p->flags^=1;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_queued_list );
	}
}

template<class T> T *gc_retain( T *t ){
#if CFG_CPP_GC_MODE==2
	*gc_locals_sp++=dynamic_cast<gc_object*>( t );
#endif	
	return t;
}

template<class T,class V> void gc_assign( T *&lhs,V *rhs ){
	gc_object *p=dynamic_cast<gc_object*>(rhs);
	if( p && (p->flags & 3)==gc_markbit ){
		p->flags^=1;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_queued_list );
	}
	lhs=rhs;
}

void gc_mark_locals(){
	for( gc_object **pp=gc_locals;pp!=gc_locals_sp;++pp ){
		gc_object *p=*pp;
		if( p && (p->flags & 3)==gc_markbit ){
			p->flags^=1;
			GC_REMOVE_NODE( p );
			GC_INSERT_NODE( p,&gc_marked_list );
			gc_marked_bytes+=(p->flags & ~7);
			p->mark();
		}
	}
}

void gc_mark_queued( int n ){
	while( gc_marked_bytes<n && !GC_LIST_IS_EMPTY( gc_queued_list ) ){
		gc_object *p=gc_queued_list.succ;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_marked_list );
		gc_marked_bytes+=(p->flags & ~7);
		p->mark();
	}
}

//returns reclaimed bytes
int gc_sweep(){

	int reclaimed_bytes=gc_alloced_bytes-gc_marked_bytes;
	
	if( reclaimed_bytes ){
	
		//append unmarked list to end of free list
		gc_object *head=gc_unmarked_list.succ;
		gc_object *tail=gc_unmarked_list.pred;
		gc_object *succ=&gc_free_list;
		gc_object *pred=succ->pred;
		head->pred=pred;
		tail->succ=succ;
		pred->succ=head;
		succ->pred=tail;
		
		gc_free_bytes+=reclaimed_bytes;
	}
	
	//move marked to unmarked.
	gc_unmarked_list=gc_marked_list;
	gc_unmarked_list.succ->pred=gc_unmarked_list.pred->succ=&gc_unmarked_list;
	
	//clear marked.
	GC_CLEAR_LIST( gc_marked_list );
	
	//adjust sizes
	gc_alloced_bytes=gc_marked_bytes;
	gc_marked_bytes=0;
	gc_markbit^=1;
	
	return reclaimed_bytes;
}

void gc_collect_all(){

//	printf( "Mark locals\n" );fflush( stdout );
	gc_mark_locals();

//	printf( "Mark queued\n" );fflush( stdout );
	gc_mark_queued( 0x7fffffff );

//	printf( "sweep\n" );fflush( stdout );	
	gc_sweep();

//	printf( "Mark roots\n" );fflush( stdout );
	gc_mark_roots();

#if DEBUG_GC	
	printf( "gc collected:%i\n",reclaimed );fflush( stdout );
#endif
}

void gc_collect(){

	if( gc_locals_sp!=gc_locals ){
//		printf( "GC_LOCALS error\n" );fflush( stdout );
		gc_locals_sp=gc_locals;
	}
	
#if CFG_CPP_GC_MODE==1

#if DEBUG_GC
	int ms=gc_micros();
#endif

	if( gc_new_bytes>(CFG_CPP_GC_TRIGGER) ){
		gc_collect_all();
		gc_new_bytes=0;
	}else{
		gc_mark_queued( (long long)(gc_new_bytes)*(gc_alloced_bytes-gc_new_bytes)/(CFG_CPP_GC_TRIGGER)+gc_new_bytes );
	}

#if DEBUG_GC
	ms=gc_micros()-ms;
	if( ms>=100 ) {printf( "gc time:%i\n",ms );fflush( stdout );}
#endif

#endif

}

// ***** Array *****

template<class T> T *t_memcpy( T *dst,const T *src,int n ){
	memcpy( dst,src,n*sizeof(T) );
	return dst+n;
}

template<class T> T *t_memset( T *dst,int val,int n ){
	memset( dst,val,n*sizeof(T) );
	return dst+n;
}

template<class T> int t_memcmp( const T *x,const T *y,int n ){
	return memcmp( x,y,n*sizeof(T) );
}

template<class T> int t_strlen( const T *p ){
	const T *q=p++;
	while( *q++ ){}
	return q-p;
}

template<class T> T *t_create( int n,T *p ){
	t_memset( p,0,n );
	return p+n;
}

template<class T> T *t_create( int n,T *p,const T *q ){
	t_memcpy( p,q,n );
	return p+n;
}

template<class T> void t_destroy( int n,T *p ){
}

template<class T> void gc_mark_elements( int n,T *p ){
}

template<class T> void gc_mark_elements( int n,T **p ){
	for( int i=0;i<n;++i ) gc_mark( p[i] );
}

template<class T> class Array{
public:
	Array():rep( &nullRep ){
	}

	//Uses default...
//	Array( const Array<T> &t )...
	
	Array( int length ):rep( Rep::alloc( length ) ){
		t_create( rep->length,rep->data );
	}
	
	Array( const T *p,int length ):rep( Rep::alloc(length) ){
		t_create( rep->length,rep->data,p );
	}
	
	~Array(){
	}

	//Uses default...
//	Array &operator=( const Array &t )...
	
	int Length()const{ 
		return rep->length; 
	}
	
	T &At( int index ){
		if( index<0 || index>=rep->length ) dbg_error( "Array index out of range" );
		return rep->data[index]; 
	}
	
	const T &At( int index )const{
		if( index<0 || index>=rep->length ) dbg_error( "Array index out of range" );
		return rep->data[index]; 
	}
	
	T &operator[]( int index ){
		return rep->data[index]; 
	}

	const T &operator[]( int index )const{
		return rep->data[index]; 
	}
	
	Array Slice( int from,int term )const{
		int len=rep->length;
		if( from<0 ){ 
			from+=len;
			if( from<0 ) from=0;
		}else if( from>len ){
			from=len;
		}
		if( term<0 ){
			term+=len;
		}else if( term>len ){
			term=len;
		}
		if( term<=from ) return Array();
		return Array( rep->data+from,term-from );
	}

	Array Slice( int from )const{
		return Slice( from,rep->length );
	}
	
	Array Resize( int newlen )const{
		if( newlen<=0 ) return Array();
		int n=rep->length;
		if( newlen<n ) n=newlen;
		Rep *p=Rep::alloc( newlen );
		T *q=p->data;
		q=t_create( n,q,rep->data );
		q=t_create( (newlen-n),q );
		return Array( p );
	}
	
private:
	struct Rep : public gc_object{
		int length;
		T data[0];
		
		Rep():length(0){
			flags=3;
		}
		
		Rep( int length ):length(length){
		}
		
		~Rep(){
			t_destroy( length,data );
		}
		
		void mark(){
			gc_mark_elements( length,data );
		}
		
		static Rep *alloc( int length ){
			if( !length ) return &nullRep;
			void *p=gc_object_alloc( sizeof(Rep)+length*sizeof(T) );
			return ::new(p) Rep( length );
		}
		
	};
	Rep *rep;
	
	static Rep nullRep;
	
	template<class C> friend void gc_mark( Array<C> t );
	template<class C> friend void gc_mark_q( Array<C> t );
	template<class C> friend Array<C> gc_retain( Array<C> t );
	template<class C> friend void gc_assign( Array<C> &lhs,Array<C> rhs );
	template<class C> friend void gc_mark_elements( int n,Array<C> *p );
	
	Array( Rep *rep ):rep(rep){
	}
};

template<class T> typename Array<T>::Rep Array<T>::nullRep;

template<class T> Array<T> *t_create( int n,Array<T> *p ){
	for( int i=0;i<n;++i ) *p++=Array<T>();
	return p;
}

template<class T> Array<T> *t_create( int n,Array<T> *p,const Array<T> *q ){
	for( int i=0;i<n;++i ) *p++=*q++;
	return p;
}

template<class T> void gc_mark( Array<T> t ){
	gc_mark( t.rep );
}

template<class T> void gc_mark_q( Array<T> t ){
	gc_mark_q( t.rep );
}

template<class T> Array<T> gc_retain( Array<T> t ){
#if CFG_CPP_GC_MODE==2
	gc_retain( t.rep );
#endif
	return t;
}

template<class T> void gc_assign( Array<T> &lhs,Array<T> rhs ){
	gc_mark( rhs.rep );
	lhs=rhs;
}

template<class T> void gc_mark_elements( int n,Array<T> *p ){
	for( int i=0;i<n;++i ) gc_mark( p[i].rep );
}
		
// ***** String *****

static const char *_str_load_err;

class String{
public:
	String():rep( &nullRep ){
	}
	
	String( const String &t ):rep( t.rep ){
		rep->retain();
	}

	String( int n ){
		char buf[256];
		sprintf_s( buf,"%i",n );
		rep=Rep::alloc( t_strlen(buf) );
		for( int i=0;i<rep->length;++i ) rep->data[i]=buf[i];
	}
	
	String( Float n ){
		char buf[256];
		
		//would rather use snprintf, but it's doing weird things in MingW.
		//
		sprintf_s( buf,"%.17lg",n );
		//
		char *p;
		for( p=buf;*p;++p ){
			if( *p=='.' || *p=='e' ) break;
		}
		if( !*p ){
			*p++='.';
			*p++='0';
			*p=0;
		}

		rep=Rep::alloc( t_strlen(buf) );
		for( int i=0;i<rep->length;++i ) rep->data[i]=buf[i];
	}

	String( Char ch,int length ):rep( Rep::alloc(length) ){
		for( int i=0;i<length;++i ) rep->data[i]=ch;
	}

	String( const Char *p ):rep( Rep::alloc(t_strlen(p)) ){
		t_memcpy( rep->data,p,rep->length );
	}

	String( const Char *p,int length ):rep( Rep::alloc(length) ){
		t_memcpy( rep->data,p,rep->length );
	}
	
#if __OBJC__	
	String( NSString *nsstr ):rep( Rep::alloc([nsstr length]) ){
		unichar *buf=(unichar*)malloc( rep->length * sizeof(unichar) );
		[nsstr getCharacters:buf range:NSMakeRange(0,rep->length)];
		for( int i=0;i<rep->length;++i ) rep->data[i]=buf[i];
		free( buf );
	}
#endif

#if __cplusplus_winrt
	String( Platform::String ^str ):rep( Rep::alloc(str->Length()) ){
		for( int i=0;i<rep->length;++i ) rep->data[i]=str->Data()[i];
	}
#endif

	~String(){
		rep->release();
	}
	
	template<class C> String( const C *p ):rep( Rep::alloc(t_strlen(p)) ){
		for( int i=0;i<rep->length;++i ) rep->data[i]=p[i];
	}
	
	template<class C> String( const C *p,int length ):rep( Rep::alloc(length) ){
		for( int i=0;i<rep->length;++i ) rep->data[i]=p[i];
	}
	
	int Length()const{
		return rep->length;
	}
	
	const Char *Data()const{
		return rep->data;
	}
	
	Char At( int index )const{
		if( index<0 || index>=rep->length ) dbg_error( "Character index out of range" );
		return rep->data[index]; 
	}
	
	Char operator[]( int index )const{
		return rep->data[index];
	}
	
	String &operator=( const String &t ){
		t.rep->retain();
		rep->release();
		rep=t.rep;
		return *this;
	}
	
	String &operator+=( const String &t ){
		return operator=( *this+t );
	}
	
	int Compare( const String &t )const{
		int n=rep->length<t.rep->length ? rep->length : t.rep->length;
		for( int i=0;i<n;++i ){
			if( int q=(int)(rep->data[i])-(int)(t.rep->data[i]) ) return q;
		}
		return rep->length-t.rep->length;
	}
	
	bool operator==( const String &t )const{
		return rep->length==t.rep->length && t_memcmp( rep->data,t.rep->data,rep->length )==0;
	}
	
	bool operator!=( const String &t )const{
		return rep->length!=t.rep->length || t_memcmp( rep->data,t.rep->data,rep->length )!=0;
	}
	
	bool operator<( const String &t )const{
		return Compare( t )<0;
	}
	
	bool operator<=( const String &t )const{
		return Compare( t )<=0;
	}
	
	bool operator>( const String &t )const{
		return Compare( t )>0;
	}
	
	bool operator>=( const String &t )const{
		return Compare( t )>=0;
	}
	
	String operator+( const String &t )const{
		if( !rep->length ) return t;
		if( !t.rep->length ) return *this;
		Rep *p=Rep::alloc( rep->length+t.rep->length );
		Char *q=p->data;
		q=t_memcpy( q,rep->data,rep->length );
		q=t_memcpy( q,t.rep->data,t.rep->length );
		return String( p );
	}
	
	int Find( String find,int start=0 )const{
		if( start<0 ) start=0;
		while( start+find.rep->length<=rep->length ){
			if( !t_memcmp( rep->data+start,find.rep->data,find.rep->length ) ) return start;
			++start;
		}
		return -1;
	}
	
	int FindLast( String find )const{
		int start=rep->length-find.rep->length;
		while( start>=0 ){
			if( !t_memcmp( rep->data+start,find.rep->data,find.rep->length ) ) return start;
			--start;
		}
		return -1;
	}
	
	int FindLast( String find,int start )const{
		if( start>rep->length-find.rep->length ) start=rep->length-find.rep->length;
		while( start>=0 ){
			if( !t_memcmp( rep->data+start,find.rep->data,find.rep->length ) ) return start;
			--start;
		}
		return -1;
	}
	
	String Trim()const{
		int i=0,i2=rep->length;
		while( i<i2 && rep->data[i]<=32 ) ++i;
		while( i2>i && rep->data[i2-1]<=32 ) --i2;
		if( i==0 && i2==rep->length ) return *this;
		return String( rep->data+i,i2-i );
	}

	Array<String> Split( String sep )const{
	
		if( !sep.rep->length ){
			Array<String> bits( rep->length );
			for( int i=0;i<rep->length;++i ){
				bits[i]=String( (Char)(*this)[i],1 );
			}
			return bits;
		}
		
		int i=0,i2,n=1;
		while( (i2=Find( sep,i ))!=-1 ){
			++n;
			i=i2+sep.rep->length;
		}
		Array<String> bits( n );
		if( n==1 ){
			bits[0]=*this;
			return bits;
		}
		i=0;n=0;
		while( (i2=Find( sep,i ))!=-1 ){
			bits[n++]=Slice( i,i2 );
			i=i2+sep.rep->length;
		}
		bits[n]=Slice( i );
		return bits;
	}

	String Join( Array<String> bits )const{
		if( bits.Length()==0 ) return String();
		if( bits.Length()==1 ) return bits[0];
		int newlen=rep->length * (bits.Length()-1);
		for( int i=0;i<bits.Length();++i ){
			newlen+=bits[i].rep->length;
		}
		Rep *p=Rep::alloc( newlen );
		Char *q=p->data;
		q=t_memcpy( q,bits[0].rep->data,bits[0].rep->length );
		for( int i=1;i<bits.Length();++i ){
			q=t_memcpy( q,rep->data,rep->length );
			q=t_memcpy( q,bits[i].rep->data,bits[i].rep->length );
		}
		return String( p );
	}

	String Replace( String find,String repl )const{
		int i=0,i2,newlen=0;
		while( (i2=Find( find,i ))!=-1 ){
			newlen+=(i2-i)+repl.rep->length;
			i=i2+find.rep->length;
		}
		if( !i ) return *this;
		newlen+=rep->length-i;
		Rep *p=Rep::alloc( newlen );
		Char *q=p->data;
		i=0;
		while( (i2=Find( find,i ))!=-1 ){
			q=t_memcpy( q,rep->data+i,i2-i );
			q=t_memcpy( q,repl.rep->data,repl.rep->length );
			i=i2+find.rep->length;
		}
		q=t_memcpy( q,rep->data+i,rep->length-i );
		return String( p );
	}

	String ToLower()const{
		for( int i=0;i<rep->length;++i ){
			Char t=towlower( rep->data[i] );
			if( t==rep->data[i] ) continue;
			Rep *p=Rep::alloc( rep->length );
			Char *q=p->data;
			t_memcpy( q,rep->data,i );
			for( q[i++]=t;i<rep->length;++i ){
				q[i]=towlower( rep->data[i] );
			}
			return String( p );
		}
		return *this;
	}

	String ToUpper()const{
		for( int i=0;i<rep->length;++i ){
			Char t=towupper( rep->data[i] );
			if( t==rep->data[i] ) continue;
			Rep *p=Rep::alloc( rep->length );
			Char *q=p->data;
			t_memcpy( q,rep->data,i );
			for( q[i++]=t;i<rep->length;++i ){
				q[i]=towupper( rep->data[i] );
			}
			return String( p );
		}
		return *this;
	}
	
	bool Contains( String sub )const{
		return Find( sub )!=-1;
	}

	bool StartsWith( String sub )const{
		return sub.rep->length<=rep->length && !t_memcmp( rep->data,sub.rep->data,sub.rep->length );
	}

	bool EndsWith( String sub )const{
		return sub.rep->length<=rep->length && !t_memcmp( rep->data+rep->length-sub.rep->length,sub.rep->data,sub.rep->length );
	}
	
	String Slice( int from,int term )const{
		int len=rep->length;
		if( from<0 ){
			from+=len;
			if( from<0 ) from=0;
		}else if( from>len ){
			from=len;
		}
		if( term<0 ){
			term+=len;
		}else if( term>len ){
			term=len;
		}
		if( term<from ) return String();
		if( from==0 && term==len ) return *this;
		return String( rep->data+from,term-from );
	}

	String Slice( int from )const{
		return Slice( from,rep->length );
	}
	
	Array<int> ToChars()const{
		Array<int> chars( rep->length );
		for( int i=0;i<rep->length;++i ) chars[i]=rep->data[i];
		return chars;
	}
	
	int ToInt()const{
		char buf[64];
		return atoi( ToCString<char>( buf,sizeof(buf) ) );
	}
	
	Float ToFloat()const{
		char buf[256];
		return atof( ToCString<char>( buf,sizeof(buf) ) );
	}

	template<class C> class CString{
		struct Rep{
			int refs;
			C data[1];
		};
		Rep *_rep;
		static Rep _nul;
	public:
		template<class T> CString( const T *data,int length ){
			_rep=(Rep*)malloc( length*sizeof(C)+sizeof(Rep) );
			_rep->refs=1;
			_rep->data[length]=0;
			for( int i=0;i<length;++i ){
				_rep->data[i]=(C)data[i];
			}
		}
		CString():_rep( new Rep ){
			_rep->refs=1;
		}
		CString( const CString &c ):_rep(c._rep){
			++_rep->refs;
		}
		~CString(){
			if( !--_rep->refs ) free( _rep );
		}
		CString &operator=( const CString &c ){
			++c._rep->refs;
			if( !--_rep->refs ) free( _rep );
			_rep=c._rep;
			return *this;
		}
		operator const C*()const{ 
			return _rep->data;
		}
	};
	
	template<class C> CString<C> ToCString()const{
		return CString<C>( rep->data,rep->length );
	}

	template<class C> C *ToCString( C *p,int length )const{
		if( --length>rep->length ) length=rep->length;
		for( int i=0;i<length;++i ) p[i]=rep->data[i];
		p[length]=0;
		return p;
	}
	
#if __OBJC__	
	NSString *ToNSString()const{
		return [NSString stringWithCharacters:ToCString<unichar>() length:rep->length];
	}
#endif

#if __cplusplus_winrt
	Platform::String ^ToWinRTString()const{
		return ref new Platform::String( rep->data,rep->length );
	}
#endif

	bool Save( FILE *fp ){
		std::vector<unsigned char> buf;
		Save( buf );
		return buf.size() ? fwrite( &buf[0],1,buf.size(),fp )==buf.size() : true;
	}
	
	void Save( std::vector<unsigned char> &buf ){
	
		Char *p=rep->data;
		Char *e=p+rep->length;
		
		while( p<e ){
			Char c=*p++;
			if( c<0x80 ){
				buf.push_back( c );
			}else if( c<0x800 ){
				buf.push_back( 0xc0 | (c>>6) );
				buf.push_back( 0x80 | (c & 0x3f) );
			}else{
				buf.push_back( 0xe0 | (c>>12) );
				buf.push_back( 0x80 | ((c>>6) & 0x3f) );
				buf.push_back( 0x80 | (c & 0x3f) );
			}
		}
	}
	
	static String FromChars( Array<int> chars ){
		int n=chars.Length();
		Rep *p=Rep::alloc( n );
		for( int i=0;i<n;++i ){
			p->data[i]=chars[i];
		}
		return String( p );
	}

	static String Load( FILE *fp ){
		unsigned char tmp[4096];
		std::vector<unsigned char> buf;
		for(;;){
			int n=fread( tmp,1,4096,fp );
			if( n>0 ) buf.insert( buf.end(),tmp,tmp+n );
			if( n!=4096 ) break;
		}
		return buf.size() ? String::Load( &buf[0],buf.size() ) : String();
	}
	
	static String Load( unsigned char *p,int n ){
	
		_str_load_err=0;
		
		unsigned char *e=p+n;
		std::vector<Char> chars;
		
		int t0=n>0 ? p[0] : -1;
		int t1=n>1 ? p[1] : -1;

		if( t0==0xfe && t1==0xff ){
			p+=2;
			while( p<e-1 ){
				int c=*p++;
				chars.push_back( (c<<8)|*p++ );
			}
		}else if( t0==0xff && t1==0xfe ){
			p+=2;
			while( p<e-1 ){
				int c=*p++;
				chars.push_back( (*p++<<8)|c );
			}
		}else{
			int t2=n>2 ? p[2] : -1;
			if( t0==0xef && t1==0xbb && t2==0xbf ) p+=3;
			unsigned char *q=p;
			bool fail=false;
			while( p<e ){
				unsigned int c=*p++;
				if( c & 0x80 ){
					if( (c & 0xe0)==0xc0 ){
						if( p>=e || (p[0] & 0xc0)!=0x80 ){
							fail=true;
							break;
						}
						c=((c & 0x1f)<<6) | (p[0] & 0x3f);
						p+=1;
					}else if( (c & 0xf0)==0xe0 ){
						if( p+1>=e || (p[0] & 0xc0)!=0x80 || (p[1] & 0xc0)!=0x80 ){
							fail=true;
							break;
						}
						c=((c & 0x0f)<<12) | ((p[0] & 0x3f)<<6) | (p[1] & 0x3f);
						p+=2;
					}else{
						fail=true;
						break;
					}
				}
				chars.push_back( c );
			}
			if( fail ){
				_str_load_err="Invalid UTF-8";
				return String( q,n );
			}
		}
		return chars.size() ? String( &chars[0],chars.size() ) : String();
	}

private:
	
	struct Rep{
		int refs;
		int length;
		Char data[0];
		
		Rep():refs(1),length(0){
		}
		
		Rep( int length ):refs(1),length(length){
		}
		
		void retain(){
			++refs;
		}
		
		void release(){
			if( --refs || !length ) return;
			free( this );
		}

		static Rep *alloc( int length ){
			if( !length ) return &nullRep;
			void *p=malloc( sizeof(Rep)+length*sizeof(Char) );
			return new(p) Rep( length );
		}
	};
	Rep *rep;
	
	static Rep nullRep;
	
	String( Rep *rep ):rep(rep){
	}
};

String::Rep String::nullRep;

String *t_create( int n,String *p ){
	for( int i=0;i<n;++i ) new( &p[i] ) String();
	return p+n;
}

String *t_create( int n,String *p,const String *q ){
	for( int i=0;i<n;++i ) new( &p[i] ) String( q[i] );
	return p+n;
}

void t_destroy( int n,String *p ){
	for( int i=0;i<n;++i ) p[i].~String();
}

// ***** Object *****

String dbg_stacktrace();

class Object : public gc_object{
public:
	virtual bool Equals( Object *obj ){
		return this==obj;
	}
	
	virtual int Compare( Object *obj ){
		return (char*)this-(char*)obj;
	}
	
	virtual String debug(){
		return "+Object\n";
	}
};

class ThrowableObject : public Object{
#ifndef NDEBUG
public:
	String stackTrace;
	ThrowableObject():stackTrace( dbg_stacktrace() ){}
#endif
};

struct gc_interface{
	virtual ~gc_interface(){}
};

//***** Debugger *****

//#define Error bbError
//#define Print bbPrint

int bbPrint( String t );

#define dbg_stream stderr

#if _MSC_VER
#define dbg_typeof decltype
#else
#define dbg_typeof __typeof__
#endif 

struct dbg_func;
struct dbg_var_type;

static int dbg_suspend;
static int dbg_stepmode;

const char *dbg_info;
String dbg_exstack;

static void *dbg_var_buf[65536*3];
static void **dbg_var_ptr=dbg_var_buf;

static dbg_func *dbg_func_buf[1024];
static dbg_func **dbg_func_ptr=dbg_func_buf;

String dbg_type( bool *p ){
	return "Bool";
}

String dbg_type( int *p ){
	return "Int";
}

String dbg_type( Float *p ){
	return "Float";
}

String dbg_type( String *p ){
	return "String";
}

template<class T> String dbg_type( T *p ){
	return "Object";
}

template<class T> String dbg_type( Array<T> *p ){
	return dbg_type( &(*p)[0] )+"[]";
}

String dbg_value( bool *p ){
	return *p ? "True" : "False";
}

String dbg_value( int *p ){
	return String( *p );
}

String dbg_value( Float *p ){
	return String( *p );
}

String dbg_value( String *p ){
	String t=*p;
	if( t.Length()>100 ) t=t.Slice( 0,100 )+"...";
	t=t.Replace( "\"","~q" );
	t=t.Replace( "\t","~t" );
	t=t.Replace( "\n","~n" );
	t=t.Replace( "\r","~r" );
	return String("\"")+t+"\"";
}

template<class T> String dbg_value( T *t ){
	Object *p=dynamic_cast<Object*>( *t );
	char buf[64];
	sprintf_s( buf,"%p",p );
	return String("@") + (buf[0]=='0' && buf[1]=='x' ? buf+2 : buf );
}

template<class T> String dbg_value( Array<T> *p ){
	String t="[";
	int n=(*p).Length();
	for( int i=0;i<n;++i ){
		if( i ) t+=",";
		t+=dbg_value( &(*p)[i] );
	}
	return t+"]";
}

template<class T> String dbg_decl( const char *id,T *ptr ){
	return String( id )+":"+dbg_type(ptr)+"="+dbg_value(ptr)+"\n";
}

struct dbg_var_type{
	virtual String type( void *p )=0;
	virtual String value( void *p )=0;
};

template<class T> struct dbg_var_type_t : public dbg_var_type{

	String type( void *p ){
		return dbg_type( (T*)p );
	}
	
	String value( void *p ){
		return dbg_value( (T*)p );
	}
	
	static dbg_var_type_t<T> info;
};
template<class T> dbg_var_type_t<T> dbg_var_type_t<T>::info;

struct dbg_blk{
	void **var_ptr;
	
	dbg_blk():var_ptr(dbg_var_ptr){
		if( dbg_stepmode=='l' ) --dbg_suspend;
	}
	
	~dbg_blk(){
		if( dbg_stepmode=='l' ) ++dbg_suspend;
		dbg_var_ptr=var_ptr;
	}
};

struct dbg_func : public dbg_blk{
	const char *id;
	const char *info;

	dbg_func( const char *p ):id(p),info(dbg_info){
		*dbg_func_ptr++=this;
		if( dbg_stepmode=='s' ) --dbg_suspend;
	}
	
	~dbg_func(){
		if( dbg_stepmode=='s' ) ++dbg_suspend;
		--dbg_func_ptr;
		dbg_info=info;
	}
};

int dbg_print( String t ){
	static char *buf;
	static int len;
	int n=t.Length();
	if( n+100>len ){
		len=n+100;
		free( buf );
		buf=(char*)malloc( len );
	}
	buf[n]='\n';
	for( int i=0;i<n;++i ) buf[i]=t[i];
	fwrite( buf,n+1,1,dbg_stream );
	fflush( dbg_stream );
	return 0;
}

void dbg_callstack(){

	void **var_ptr=dbg_var_buf;
	dbg_func **func_ptr=dbg_func_buf;
	
	while( var_ptr!=dbg_var_ptr ){
		while( func_ptr!=dbg_func_ptr && var_ptr==(*func_ptr)->var_ptr ){
			const char *id=(*func_ptr++)->id;
			const char *info=func_ptr!=dbg_func_ptr ? (*func_ptr)->info : dbg_info;
			fprintf( dbg_stream,"+%s;%s\n",id,info );
		}
		void *vp=*var_ptr++;
		const char *nm=(const char*)*var_ptr++;
		dbg_var_type *ty=(dbg_var_type*)*var_ptr++;
		dbg_print( String(nm)+":"+ty->type(vp)+"="+ty->value(vp) );
	}
	while( func_ptr!=dbg_func_ptr ){
		const char *id=(*func_ptr++)->id;
		const char *info=func_ptr!=dbg_func_ptr ? (*func_ptr)->info : dbg_info;
		fprintf( dbg_stream,"+%s;%s\n",id,info );
	}
}

String dbg_stacktrace(){
	if( !dbg_info || !dbg_info[0] ) return "";
	String str=String( dbg_info )+"\n";
	dbg_func **func_ptr=dbg_func_ptr;
	if( func_ptr==dbg_func_buf ) return str;
	while( --func_ptr!=dbg_func_buf ){
		str+=String( (*func_ptr)->info )+"\n";
	}
	return str;
}

void dbg_throw( const char *err ){
	dbg_exstack=dbg_stacktrace();
	throw err;
}

void dbg_stop(){

#if TARGET_OS_IPHONE
	dbg_throw( "STOP" );
#endif

	fprintf( dbg_stream,"{{~~%s~~}}\n",dbg_info );
	dbg_callstack();
	dbg_print( "" );
	
	for(;;){

		char buf[256];
		char *e=fgets( buf,256,stdin );
		if( !e ) exit( -1 );
		
		e=strchr( buf,'\n' );
		if( !e ) exit( -1 );
		
		*e=0;
		
		Object *p;
		
		switch( buf[0] ){
		case '?':
			break;
		case 'r':	//run
			dbg_suspend=0;		
			dbg_stepmode=0;
			return;
		case 's':	//step
			dbg_suspend=1;
			dbg_stepmode='s';
			return;
		case 'e':	//enter func
			dbg_suspend=1;
			dbg_stepmode='e';
			return;
		case 'l':	//leave block
			dbg_suspend=0;
			dbg_stepmode='l';
			return;
		case '@':	//dump object
			p=0;
			sscanf_s( buf+1,"%p",&p );
			if( p ){
				dbg_print( p->debug() );
			}else{
				dbg_print( "" );
			}
			break;
		case 'q':	//quit!
			exit( 0 );
			break;			
		default:
			printf( "????? %s ?????",buf );fflush( stdout );
			exit( -1 );
		}
	}
}

void dbg_error( const char *err ){

#if TARGET_OS_IPHONE
	dbg_throw( err );
#endif

	for(;;){
		bbPrint( String("Monkey Runtime Error : ")+err );
		bbPrint( dbg_stacktrace() );
		dbg_stop();
	}
}

#define DBG_INFO(X) dbg_info=(X);if( dbg_suspend>0 ) dbg_stop();

#define DBG_ENTER(P) dbg_func _dbg_func(P);

#define DBG_BLOCK() dbg_blk _dbg_blk;

#define DBG_GLOBAL( ID,NAME )	//TODO!

#define DBG_LOCAL( ID,NAME )\
*dbg_var_ptr++=&ID;\
*dbg_var_ptr++=(void*)NAME;\
*dbg_var_ptr++=&dbg_var_type_t<dbg_typeof(ID)>::info;

//**** main ****

int argc;
const char **argv;

Float D2R=0.017453292519943295f;
Float R2D=57.29577951308232f;

int bbPrint( String t ){

	static std::vector<unsigned char> buf;
	buf.clear();
	t.Save( buf );
	buf.push_back( '\n' );
	buf.push_back( 0 );
	
#if __cplusplus_winrt	//winrt?

#if CFG_WINRT_PRINT_ENABLED
	OutputDebugStringA( (const char*)&buf[0] );
#endif

#elif _WIN32			//windows?

	fputs( (const char*)&buf[0],stdout );
	fflush( stdout );

#elif __APPLE__			//macos/ios?

	fputs( (const char*)&buf[0],stdout );
	fflush( stdout );
	
#elif __linux			//linux?

#if CFG_ANDROID_NDK_PRINT_ENABLED
	LOGI( (const char*)&buf[0] );
#else
	fputs( (const char*)&buf[0],stdout );
	fflush( stdout );
#endif

#endif

	return 0;
}

class BBExitApp{
};

int bbError( String err ){
	if( !err.Length() ){
#if __cplusplus_winrt
		throw BBExitApp();
#else
		exit( 0 );
#endif
	}
	dbg_error( err.ToCString<char>() );
	return 0;
}

int bbDebugLog( String t ){
	bbPrint( t );
	return 0;
}

int bbDebugStop(){
	dbg_stop();
	return 0;
}

int bbInit();
int bbMain();

#if _MSC_VER

static void _cdecl seTranslator( unsigned int ex,EXCEPTION_POINTERS *p ){

	switch( ex ){
	case EXCEPTION_ACCESS_VIOLATION:dbg_error( "Memory access violation" );
	case EXCEPTION_ILLEGAL_INSTRUCTION:dbg_error( "Illegal instruction" );
	case EXCEPTION_INT_DIVIDE_BY_ZERO:dbg_error( "Integer divide by zero" );
	case EXCEPTION_STACK_OVERFLOW:dbg_error( "Stack overflow" );
	}
	dbg_error( "Unknown exception" );
}

#else

void sighandler( int sig  ){
	switch( sig ){
	case SIGSEGV:dbg_error( "Memory access violation" );
	case SIGILL:dbg_error( "Illegal instruction" );
	case SIGFPE:dbg_error( "Floating point exception" );
#if !_WIN32
	case SIGBUS:dbg_error( "Bus error" );
#endif	
	}
	dbg_error( "Unknown signal" );
}

#endif

//entry point call by target main()...
//
int bb_std_main( int argc,const char **argv ){

	::argc=argc;
	::argv=argv;
	
#if _MSC_VER

	_set_se_translator( seTranslator );

#else
	
	signal( SIGSEGV,sighandler );
	signal( SIGILL,sighandler );
	signal( SIGFPE,sighandler );
#if !_WIN32
	signal( SIGBUS,sighandler );
#endif

#endif

	if( !setlocale( LC_CTYPE,"en_US.UTF-8" ) ){
		setlocale( LC_CTYPE,"" );
	}

	gc_init1();

	bbInit();
	
	gc_init2();

	bbMain();

	return 0;
}


//***** game.h *****

struct BBGameEvent{
	enum{
		None=0,
		KeyDown=1,KeyUp=2,KeyChar=3,
		MouseDown=4,MouseUp=5,MouseMove=6,
		TouchDown=7,TouchUp=8,TouchMove=9,
		MotionAccel=10
	};
};

class BBGameDelegate : public Object{
public:
	virtual void StartGame(){}
	virtual void SuspendGame(){}
	virtual void ResumeGame(){}
	virtual void UpdateGame(){}
	virtual void RenderGame(){}
	virtual void KeyEvent( int event,int data ){}
	virtual void MouseEvent( int event,int data,float x,float y ){}
	virtual void TouchEvent( int event,int data,float x,float y ){}
	virtual void MotionEvent( int event,int data,float x,float y,float z ){}
	virtual void DiscardGraphics(){}
};

struct BBDisplayMode : public Object{
	int width;
	int height;
	int format;
	int hertz;
	int flags;
	BBDisplayMode( int width=0,int height=0,int format=0,int hertz=0,int flags=0 ):width(width),height(height),format(format),hertz(hertz),flags(flags){}
};

class BBGame{
public:
	BBGame();
	virtual ~BBGame(){}
	
	// ***** Extern *****
	static BBGame *Game(){ return _game; }
	
	virtual void SetDelegate( BBGameDelegate *delegate );
	virtual BBGameDelegate *Delegate(){ return _delegate; }
	
	virtual void SetKeyboardEnabled( bool enabled );
	virtual bool KeyboardEnabled();
	
	virtual void SetUpdateRate( int updateRate );
	virtual int UpdateRate();
	
	virtual bool Started(){ return _started; }
	virtual bool Suspended(){ return _suspended; }
	
	virtual int Millisecs();
	virtual void GetDate( Array<int> date );
	virtual int SaveState( String state );
	virtual String LoadState();
	virtual String LoadString( String path );
	virtual bool PollJoystick( int port,Array<Float> joyx,Array<Float> joyy,Array<Float> joyz,Array<bool> buttons );
	virtual void OpenUrl( String url );
	virtual void SetMouseVisible( bool visible );
	
	virtual int GetDeviceWidth(){ return 0; }
	virtual int GetDeviceHeight(){ return 0; }
	virtual void SetDeviceWindow( int width,int height,int flags ){}
	virtual Array<BBDisplayMode*> GetDisplayModes(){ return Array<BBDisplayMode*>(); }
	virtual BBDisplayMode *GetDesktopMode(){ return 0; }
	virtual void SetSwapInterval( int interval ){}

	// ***** Native *****	
	virtual String PathToFilePath( String path );
	virtual FILE *OpenFile( String path,String mode );
	virtual unsigned char *LoadData( String path,int *plength );
	virtual unsigned char *LoadImageData( String path,int *width,int *height,int *depth ){ return 0; }
	virtual unsigned char *LoadAudioData( String path,int *length,int *channels,int *format,int *hertz ){ return 0; }
	
	//***** Internal *****
	virtual void Die( ThrowableObject *ex );
	virtual void gc_collect();
	virtual void StartGame();
	virtual void SuspendGame();
	virtual void ResumeGame();
	virtual void UpdateGame();
	virtual void RenderGame();
	virtual void KeyEvent( int ev,int data );
	virtual void MouseEvent( int ev,int data,float x,float y );
	virtual void TouchEvent( int ev,int data,float x,float y );
	virtual void MotionEvent( int ev,int data,float x,float y,float z );
	virtual void DiscardGraphics();
	
protected:

	static BBGame *_game;

	BBGameDelegate *_delegate;
	bool _keyboardEnabled;
	int _updateRate;
	bool _started;
	bool _suspended;
};

//***** game.cpp *****

BBGame *BBGame::_game;

BBGame::BBGame():
_delegate( 0 ),
_keyboardEnabled( false ),
_updateRate( 0 ),
_started( false ),
_suspended( false ){
	_game=this;
}

void BBGame::SetDelegate( BBGameDelegate *delegate ){
	_delegate=delegate;
}

void BBGame::SetKeyboardEnabled( bool enabled ){
	_keyboardEnabled=enabled;
}

bool BBGame::KeyboardEnabled(){
	return _keyboardEnabled;
}

void BBGame::SetUpdateRate( int updateRate ){
	_updateRate=updateRate;
}

int BBGame::UpdateRate(){
	return _updateRate;
}

int BBGame::Millisecs(){
	return 0;
}

void BBGame::GetDate( Array<int> date ){
	int n=date.Length();
	if( n>0 ){
		time_t t=time( 0 );
		
#if _MSC_VER
		struct tm tii;
		struct tm *ti=&tii;
		localtime_s( ti,&t );
#else
		struct tm *ti=localtime( &t );
#endif

		date[0]=ti->tm_year+1900;
		if( n>1 ){ 
			date[1]=ti->tm_mon+1;
			if( n>2 ){
				date[2]=ti->tm_mday;
				if( n>3 ){
					date[3]=ti->tm_hour;
					if( n>4 ){
						date[4]=ti->tm_min;
						if( n>5 ){
							date[5]=ti->tm_sec;
							if( n>6 ){
								date[6]=0;
							}
						}
					}
				}
			}
		}
	}
}

int BBGame::SaveState( String state ){
	if( FILE *f=OpenFile( "./.monkeystate","wb" ) ){
		bool ok=state.Save( f );
		fclose( f );
		return ok ? 0 : -2;
	}
	return -1;
}

String BBGame::LoadState(){
	if( FILE *f=OpenFile( "./.monkeystate","rb" ) ){
		String str=String::Load( f );
		fclose( f );
		return str;
	}
	return "";
}

String BBGame::LoadString( String path ){
	if( FILE *fp=OpenFile( path,"rb" ) ){
		String str=String::Load( fp );
		fclose( fp );
		return str;
	}
	return "";
}

bool BBGame::PollJoystick( int port,Array<Float> joyx,Array<Float> joyy,Array<Float> joyz,Array<bool> buttons ){
	return false;
}

void BBGame::OpenUrl( String url ){
}

void BBGame::SetMouseVisible( bool visible ){
}

//***** C++ Game *****

String BBGame::PathToFilePath( String path ){
	return path;
}

FILE *BBGame::OpenFile( String path,String mode ){
	path=PathToFilePath( path );
	if( path=="" ) return 0;
	
#if __cplusplus_winrt
	path=path.Replace( "/","\\" );
	FILE *f;
	if( _wfopen_s( &f,path.ToCString<wchar_t>(),mode.ToCString<wchar_t>() ) ) return 0;
	return f;
#elif _WIN32
	return _wfopen( path.ToCString<wchar_t>(),mode.ToCString<wchar_t>() );
#else
	return fopen( path.ToCString<char>(),mode.ToCString<char>() );
#endif
}

unsigned char *BBGame::LoadData( String path,int *plength ){

	FILE *f=OpenFile( path,"rb" );
	if( !f ) return 0;

	const int BUF_SZ=4096;
	std::vector<void*> tmps;
	int length=0;
	
	for(;;){
		void *p=malloc( BUF_SZ );
		int n=fread( p,1,BUF_SZ,f );
		tmps.push_back( p );
		length+=n;
		if( n!=BUF_SZ ) break;
	}
	fclose( f );
	
	unsigned char *data=(unsigned char*)malloc( length );
	unsigned char *p=data;
	
	int sz=length;
	for( int i=0;i<tmps.size();++i ){
		int n=sz>BUF_SZ ? BUF_SZ : sz;
		memcpy( p,tmps[i],n );
		free( tmps[i] );
		sz-=n;
		p+=n;
	}
	
	*plength=length;
	
	gc_ext_malloced( length );
	
	return data;
}

//***** INTERNAL *****

void BBGame::Die( ThrowableObject *ex ){
	bbPrint( "Monkey Runtime Error : Uncaught Monkey Exception" );
#ifndef NDEBUG
	bbPrint( ex->stackTrace );
#endif
	exit( -1 );
}

void BBGame::gc_collect(){
	gc_mark( _delegate );
	::gc_collect();
}

void BBGame::StartGame(){

	if( _started ) return;
	_started=true;
	
	try{
		_delegate->StartGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::SuspendGame(){

	if( !_started || _suspended ) return;
	_suspended=true;
	
	try{
		_delegate->SuspendGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::ResumeGame(){

	if( !_started || !_suspended ) return;
	_suspended=false;
	
	try{
		_delegate->ResumeGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::UpdateGame(){

	if( !_started || _suspended ) return;
	
	try{
		_delegate->UpdateGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::RenderGame(){

	if( !_started ) return;
	
	try{
		_delegate->RenderGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::KeyEvent( int ev,int data ){

	if( !_started ) return;
	
	try{
		_delegate->KeyEvent( ev,data );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::MouseEvent( int ev,int data,float x,float y ){

	if( !_started ) return;
	
	try{
		_delegate->MouseEvent( ev,data,x,y );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::TouchEvent( int ev,int data,float x,float y ){

	if( !_started ) return;
	
	try{
		_delegate->TouchEvent( ev,data,x,y );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::MotionEvent( int ev,int data,float x,float y,float z ){

	if( !_started ) return;
	
	try{
		_delegate->MotionEvent( ev,data,x,y,z );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::DiscardGraphics(){

	if( !_started ) return;
	
	try{
		_delegate->DiscardGraphics();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}


//***** wavloader.h *****
//
unsigned char *LoadWAV( FILE *f,int *length,int *channels,int *format,int *hertz );

//***** wavloader.cpp *****
//
static const char *readTag( FILE *f ){
	static char buf[8];
	if( fread( buf,4,1,f )!=1 ) return "";
	buf[4]=0;
	return buf;
}

static int readInt( FILE *f ){
	unsigned char buf[4];
	if( fread( buf,4,1,f )!=1 ) return -1;
	return (buf[3]<<24) | (buf[2]<<16) | (buf[1]<<8) | buf[0];
}

static int readShort( FILE *f ){
	unsigned char buf[2];
	if( fread( buf,2,1,f )!=1 ) return -1;
	return (buf[1]<<8) | buf[0];
}

static void skipBytes( int n,FILE *f ){
	char *p=(char*)malloc( n );
	fread( p,n,1,f );
	free( p );
}

unsigned char *LoadWAV( FILE *f,int *plength,int *pchannels,int *pformat,int *phertz ){
	if( !strcmp( readTag( f ),"RIFF" ) ){
		int len=readInt( f )-8;len=len;
		if( !strcmp( readTag( f ),"WAVE" ) ){
			if( !strcmp( readTag( f ),"fmt " ) ){
				int len2=readInt( f );
				int comp=readShort( f );
				if( comp==1 ){
					int chans=readShort( f );
					int hertz=readInt( f );
					int bytespersec=readInt( f );bytespersec=bytespersec;
					int pad=readShort( f );pad=pad;
					int bits=readShort( f );
					int format=bits/8;
					if( len2>16 ) skipBytes( len2-16,f );
					for(;;){
						const char *p=readTag( f );
						if( feof( f ) ) break;
						int size=readInt( f );
						if( strcmp( p,"data" ) ){
							skipBytes( size,f );
							continue;
						}
						unsigned char *data=(unsigned char*)malloc( size );
						if( fread( data,size,1,f )==1 ){
							*plength=size/(chans*format);
							*pchannels=chans;
							*pformat=format;
							*phertz=hertz;
							return data;
						}
						free( data );
					}
				}
			}
		}
	}
	return 0;
}



//***** oggloader.h *****
unsigned char *LoadOGG( FILE *f,int *length,int *channels,int *format,int *hertz );

//***** oggloader.cpp *****
unsigned char *LoadOGG( FILE *f,int *length,int *channels,int *format,int *hertz ){

	int error;
	stb_vorbis *v=stb_vorbis_open_file( f,0,&error,0 );
	if( !v ) return 0;
	
	stb_vorbis_info info=stb_vorbis_get_info( v );
	
	int limit=info.channels*4096;
	int offset=0,total=limit;

	short *data=(short*)malloc( total*2 );
	
	for(;;){
		int n=stb_vorbis_get_frame_short_interleaved( v,info.channels,data+offset,total-offset );
		if( !n ) break;
	
		offset+=n*info.channels;
		
		if( offset+limit>total ){
			total*=2;
			data=(short*)realloc( data,total*2 );
		}
	}
	
	data=(short*)realloc( data,offset*2 );
	
	*length=offset/info.channels;
	*channels=info.channels;
	*format=2;
	*hertz=info.sample_rate;
	
	stb_vorbis_close(v);
	
	return (unsigned char*)data;
}



//***** glfwgame.h *****

struct BBGlfwVideoMode : public Object{
	int Width;
	int Height;
	int RedBits;
	int GreenBits;
	int BlueBits;
	BBGlfwVideoMode( int w,int h,int r,int g,int b ):Width(w),Height(h),RedBits(r),GreenBits(g),BlueBits(b){}
};

class BBGlfwGame : public BBGame{
public:
	BBGlfwGame();

	static BBGlfwGame *GlfwGame(){ return _glfwGame; }
	
	virtual void SetUpdateRate( int hertz );
	virtual int Millisecs();
	virtual bool PollJoystick( int port,Array<Float> joyx,Array<Float> joyy,Array<Float> joyz,Array<bool> buttons );
	virtual void OpenUrl( String url );
	virtual void SetMouseVisible( bool visible );
	
	virtual int GetDeviceWidth();
	virtual int GetDeviceHeight();
	virtual void SetDeviceWindow( int width,int height,int flags );
	virtual Array<BBDisplayMode*> GetDisplayModes();
	virtual BBDisplayMode *GetDesktopMode();
	virtual void SetSwapInterval( int interval );

	virtual String PathToFilePath( String path );
	virtual unsigned char *LoadImageData( String path,int *width,int *height,int *depth );
	virtual unsigned char *LoadAudioData( String path,int *length,int *channels,int *format,int *hertz );
	
	virtual void SetGlfwWindow( int width,int height,int red,int green,int blue,int alpha,int depth,int stencil,bool fullscreen );
	virtual BBGlfwVideoMode *GetGlfwDesktopMode();
	virtual Array<BBGlfwVideoMode*> GetGlfwVideoModes();
	
	virtual void Run();
	
private:
	static BBGlfwGame *_glfwGame;

	double _updatePeriod;
	double _nextUpdate;
	
	int _swapInterval;
	
	void UpdateEvents();
		
protected:
	static int TransKey( int key );
	static int KeyToChar( int key );
	
	static void GLFWCALL OnKey( int key,int action );
	static void GLFWCALL OnChar( int chr,int action );
	static void GLFWCALL OnMouseButton( int button,int action );
	static void GLFWCALL OnMousePos( int x,int y );
	static int  GLFWCALL OnWindowClose();
};

//***** glfwgame.cpp *****

#define _QUOTE(X) #X
#define _STRINGIZE( X ) _QUOTE(X)

enum{
	VKEY_BACKSPACE=8,VKEY_TAB,
	VKEY_ENTER=13,
	VKEY_SHIFT=16,
	VKEY_CONTROL=17,
	VKEY_ESC=27,
	VKEY_SPACE=32,
	VKEY_PAGEUP=33,VKEY_PAGEDOWN,VKEY_END,VKEY_HOME,
	VKEY_LEFT=37,VKEY_UP,VKEY_RIGHT,VKEY_DOWN,
	VKEY_INSERT=45,VKEY_DELETE,
	VKEY_0=48,VKEY_1,VKEY_2,VKEY_3,VKEY_4,VKEY_5,VKEY_6,VKEY_7,VKEY_8,VKEY_9,
	VKEY_A=65,VKEY_B,VKEY_C,VKEY_D,VKEY_E,VKEY_F,VKEY_G,VKEY_H,VKEY_I,VKEY_J,
	VKEY_K,VKEY_L,VKEY_M,VKEY_N,VKEY_O,VKEY_P,VKEY_Q,VKEY_R,VKEY_S,VKEY_T,
	VKEY_U,VKEY_V,VKEY_W,VKEY_X,VKEY_Y,VKEY_Z,
	
	VKEY_LSYS=91,VKEY_RSYS,
	
	VKEY_NUM0=96,VKEY_NUM1,VKEY_NUM2,VKEY_NUM3,VKEY_NUM4,
	VKEY_NUM5,VKEY_NUM6,VKEY_NUM7,VKEY_NUM8,VKEY_NUM9,
	VKEY_NUMMULTIPLY=106,VKEY_NUMADD,VKEY_NUMSLASH,
	VKEY_NUMSUBTRACT,VKEY_NUMDECIMAL,VKEY_NUMDIVIDE,

	VKEY_F1=112,VKEY_F2,VKEY_F3,VKEY_F4,VKEY_F5,VKEY_F6,
	VKEY_F7,VKEY_F8,VKEY_F9,VKEY_F10,VKEY_F11,VKEY_F12,

	VKEY_LSHIFT=160,VKEY_RSHIFT,
	VKEY_LCONTROL=162,VKEY_RCONTROL,
	VKEY_LALT=164,VKEY_RALT,

	VKEY_TILDE=192,VKEY_MINUS=189,VKEY_EQUALS=187,
	VKEY_OPENBRACKET=219,VKEY_BACKSLASH=220,VKEY_CLOSEBRACKET=221,
	VKEY_SEMICOLON=186,VKEY_QUOTES=222,
	VKEY_COMMA=188,VKEY_PERIOD=190,VKEY_SLASH=191
};

BBGlfwGame *BBGlfwGame::_glfwGame;

BBGlfwGame::BBGlfwGame():_updatePeriod(0),_nextUpdate(0),_swapInterval( CFG_GLFW_SWAP_INTERVAL ){
	_glfwGame=this;
}

//***** BBGame *****

void Init_GL_Exts();

int glfwGraphicsSeq=0;

void BBGlfwGame::SetUpdateRate( int updateRate ){
	BBGame::SetUpdateRate( updateRate );
	if( _updateRate ) _updatePeriod=1.0/_updateRate;
	_nextUpdate=0;
}

int BBGlfwGame::Millisecs(){
	return int( glfwGetTime()*1000.0 );
}

bool BBGlfwGame::PollJoystick( int port,Array<Float> joyx,Array<Float> joyy,Array<Float> joyz,Array<bool> buttons ){

	int joy=GLFW_JOYSTICK_1+port;
	if( !glfwGetJoystickParam( joy,GLFW_PRESENT ) ) return false;

	//read axes
	float axes[6];
	memset( axes,0,sizeof(axes) );
	int n_axes=glfwGetJoystickPos( joy,axes,6 );
	joyx[0]=axes[0];joyy[0]=axes[1];joyz[0]=axes[2];
	joyx[1]=axes[3];joyy[1]=axes[4];joyz[1]=axes[5];
	
	//read buttons
	unsigned char buts[32];
	memset( buts,0,sizeof(buts) );
	int n_buts=glfwGetJoystickButtons( joy,buts,32 );
	if( n_buts>12 ){
		for( int i=0;i<8;++i ) buttons[i]=(buts[i]==GLFW_PRESS);
		for( int i=0;i<4;++i ) buttons[i+8]=(buts[n_buts-4+i]==GLFW_PRESS);
		for( int i=0;i<n_buts-12;++i ) buttons[i+12]=(buts[i+8]==GLFW_PRESS);
	}else{
		for( int i=0;i<n_buts;++i ) buttons[i]=(buts[i]=-GLFW_PRESS);
	}
	
	//kludges for device type!
	if( n_axes==5 && n_buts==14 ){
		//XBOX_360?
		joyx[1]=axes[4];
		joyy[1]=-axes[3];
	}else if( n_axes==4 && n_buts==18 ){
		//My Saitek?
		joyy[1]=-joyz[0];
	}
	
	//enough!
	return true;
}

void BBGlfwGame::OpenUrl( String url ){
#if _WIN32
	ShellExecute( HWND_DESKTOP,"open",url.ToCString<char>(),0,0,SW_SHOWNORMAL );
#elif __APPLE__
	if( CFURLRef cfurl=CFURLCreateWithBytes( 0,url.ToCString<UInt8>(),url.Length(),kCFStringEncodingASCII,0 ) ){
		LSOpenCFURLRef( cfurl,0 );
		CFRelease( cfurl );
	}
#elif __linux
	system( ( String( "xdg-open \"" )+url+"\"" ).ToCString<char>() );
#endif
}

void BBGlfwGame::SetMouseVisible( bool visible ){
	if( visible ){
		glfwEnable( GLFW_MOUSE_CURSOR );
	}else{
		glfwDisable( GLFW_MOUSE_CURSOR );
	}
}

String BBGlfwGame::PathToFilePath( String path ){
	if( !path.StartsWith( "monkey:" ) ){
		return path;
	}else if( path.StartsWith( "monkey://data/" ) ){
		return String("./data/")+path.Slice(14);
	}else if( path.StartsWith( "monkey://internal/" ) ){
		return String("./internal/")+path.Slice(18);
	}else if( path.StartsWith( "monkey://external/" ) ){
		return String("./external/")+path.Slice(18);
	}
	return "";
}

unsigned char *BBGlfwGame::LoadImageData( String path,int *width,int *height,int *depth ){

	FILE *f=OpenFile( path,"rb" );
	if( !f ) return 0;
	
	unsigned char *data=stbi_load_from_file( f,width,height,depth,0 );
	fclose( f );
	
	if( data ) gc_ext_malloced( (*width)*(*height)*(*depth) );
	
	return data;
}

unsigned char *BBGlfwGame::LoadAudioData( String path,int *length,int *channels,int *format,int *hertz ){

	FILE *f=OpenFile( path,"rb" );
	if( !f ) return 0;
	
	unsigned char *data=0;
	
	if( path.ToLower().EndsWith( ".wav" ) ){
		data=LoadWAV( f,length,channels,format,hertz );
	}else if( path.ToLower().EndsWith( ".ogg" ) ){
		data=LoadOGG( f,length,channels,format,hertz );
	}
	fclose( f );
	
	if( data ) gc_ext_malloced( (*length)*(*channels)*(*format) );
	
	return data;
}

//glfw key to monkey key!
int BBGlfwGame::TransKey( int key ){

	if( key>='0' && key<='9' ) return key;
	if( key>='A' && key<='Z' ) return key;

	switch( key ){

	case ' ':return VKEY_SPACE;
	case ';':return VKEY_SEMICOLON;
	case '=':return VKEY_EQUALS;
	case ',':return VKEY_COMMA;
	case '-':return VKEY_MINUS;
	case '.':return VKEY_PERIOD;
	case '/':return VKEY_SLASH;
	case '~':return VKEY_TILDE;
	case '[':return VKEY_OPENBRACKET;
	case ']':return VKEY_CLOSEBRACKET;
	case '\"':return VKEY_QUOTES;
	case '\\':return VKEY_BACKSLASH;
	
	case '`':return VKEY_TILDE;
	case '\'':return VKEY_QUOTES;

	case GLFW_KEY_LSHIFT:
	case GLFW_KEY_RSHIFT:return VKEY_SHIFT;
	case GLFW_KEY_LCTRL:
	case GLFW_KEY_RCTRL:return VKEY_CONTROL;
	
//	case GLFW_KEY_LSHIFT:return VKEY_LSHIFT;
//	case GLFW_KEY_RSHIFT:return VKEY_RSHIFT;
//	case GLFW_KEY_LCTRL:return VKEY_LCONTROL;
//	case GLFW_KEY_RCTRL:return VKEY_RCONTROL;
	
	case GLFW_KEY_BACKSPACE:return VKEY_BACKSPACE;
	case GLFW_KEY_TAB:return VKEY_TAB;
	case GLFW_KEY_ENTER:return VKEY_ENTER;
	case GLFW_KEY_ESC:return VKEY_ESC;
	case GLFW_KEY_INSERT:return VKEY_INSERT;
	case GLFW_KEY_DEL:return VKEY_DELETE;
	case GLFW_KEY_PAGEUP:return VKEY_PAGEUP;
	case GLFW_KEY_PAGEDOWN:return VKEY_PAGEDOWN;
	case GLFW_KEY_HOME:return VKEY_HOME;
	case GLFW_KEY_END:return VKEY_END;
	case GLFW_KEY_UP:return VKEY_UP;
	case GLFW_KEY_DOWN:return VKEY_DOWN;
	case GLFW_KEY_LEFT:return VKEY_LEFT;
	case GLFW_KEY_RIGHT:return VKEY_RIGHT;
	
	case GLFW_KEY_KP_0:return VKEY_NUM0;
	case GLFW_KEY_KP_1:return VKEY_NUM1;
	case GLFW_KEY_KP_2:return VKEY_NUM2;
	case GLFW_KEY_KP_3:return VKEY_NUM3;
	case GLFW_KEY_KP_4:return VKEY_NUM4;
	case GLFW_KEY_KP_5:return VKEY_NUM5;
	case GLFW_KEY_KP_6:return VKEY_NUM6;
	case GLFW_KEY_KP_7:return VKEY_NUM7;
	case GLFW_KEY_KP_8:return VKEY_NUM8;
	case GLFW_KEY_KP_9:return VKEY_NUM9;
	case GLFW_KEY_KP_DIVIDE:return VKEY_NUMDIVIDE;
	case GLFW_KEY_KP_MULTIPLY:return VKEY_NUMMULTIPLY;
	case GLFW_KEY_KP_SUBTRACT:return VKEY_NUMSUBTRACT;
	case GLFW_KEY_KP_ADD:return VKEY_NUMADD;
	case GLFW_KEY_KP_DECIMAL:return VKEY_NUMDECIMAL;
    	
	case GLFW_KEY_F1:return VKEY_F1;
	case GLFW_KEY_F2:return VKEY_F2;
	case GLFW_KEY_F3:return VKEY_F3;
	case GLFW_KEY_F4:return VKEY_F4;
	case GLFW_KEY_F5:return VKEY_F5;
	case GLFW_KEY_F6:return VKEY_F6;
	case GLFW_KEY_F7:return VKEY_F7;
	case GLFW_KEY_F8:return VKEY_F8;
	case GLFW_KEY_F9:return VKEY_F9;
	case GLFW_KEY_F10:return VKEY_F10;
	case GLFW_KEY_F11:return VKEY_F11;
	case GLFW_KEY_F12:return VKEY_F12;
	}
	return 0;
}

//monkey key to special monkey char
int BBGlfwGame::KeyToChar( int key ){
	switch( key ){
	case VKEY_BACKSPACE:
	case VKEY_TAB:
	case VKEY_ENTER:
	case VKEY_ESC:
		return key;
	case VKEY_PAGEUP:
	case VKEY_PAGEDOWN:
	case VKEY_END:
	case VKEY_HOME:
	case VKEY_LEFT:
	case VKEY_UP:
	case VKEY_RIGHT:
	case VKEY_DOWN:
	case VKEY_INSERT:
		return key | 0x10000;
	case VKEY_DELETE:
		return 127;
	}
	return 0;
}

void BBGlfwGame::OnMouseButton( int button,int action ){
	switch( button ){
	case GLFW_MOUSE_BUTTON_LEFT:button=0;break;
	case GLFW_MOUSE_BUTTON_RIGHT:button=1;break;
	case GLFW_MOUSE_BUTTON_MIDDLE:button=2;break;
	default:return;
	}
	int x,y;
	glfwGetMousePos( &x,&y );
	switch( action ){
	case GLFW_PRESS:
		_glfwGame->MouseEvent( BBGameEvent::MouseDown,button,x,y );
		break;
	case GLFW_RELEASE:
		_glfwGame->MouseEvent( BBGameEvent::MouseUp,button,x,y );
		break;
	}
}

void BBGlfwGame::OnMousePos( int x,int y ){
	_game->MouseEvent( BBGameEvent::MouseMove,-1,x,y );
}

int BBGlfwGame::OnWindowClose(){
	_game->KeyEvent( BBGameEvent::KeyDown,0x1b0 );
	_game->KeyEvent( BBGameEvent::KeyUp,0x1b0 );
	return GL_FALSE;
}

void BBGlfwGame::OnKey( int key,int action ){

	key=TransKey( key );
	if( !key ) return;
	
	switch( action ){
	case GLFW_PRESS:
		_glfwGame->KeyEvent( BBGameEvent::KeyDown,key );
		if( int chr=KeyToChar( key ) ) _game->KeyEvent( BBGameEvent::KeyChar,chr );
		break;
	case GLFW_RELEASE:
		_glfwGame->KeyEvent( BBGameEvent::KeyUp,key );
		break;
	}
}

void BBGlfwGame::OnChar( int chr,int action ){

	switch( action ){
	case GLFW_PRESS:
		_glfwGame->KeyEvent( BBGameEvent::KeyChar,chr );
		break;
	}
}

void BBGlfwGame::SetGlfwWindow( int width,int height,int red,int green,int blue,int alpha,int depth,int stencil,bool fullscreen ){

	for( int i=0;i<=GLFW_KEY_LAST;++i ){
		int key=TransKey( i );
		if( key && glfwGetKey( i )==GLFW_PRESS ) KeyEvent( BBGameEvent::KeyUp,key );
	}

	GLFWvidmode desktopMode;
	glfwGetDesktopMode( &desktopMode );

	glfwCloseWindow();
	
	glfwOpenWindowHint( GLFW_REFRESH_RATE,60 );
	glfwOpenWindowHint( GLFW_WINDOW_NO_RESIZE,CFG_GLFW_WINDOW_RESIZABLE ? GL_FALSE : GL_TRUE );

	glfwOpenWindow( width,height,red,green,blue,alpha,depth,stencil,fullscreen ? GLFW_FULLSCREEN : GLFW_WINDOW );

	++glfwGraphicsSeq;

	if( !fullscreen ){	
		glfwSetWindowPos( (desktopMode.Width-width)/2,(desktopMode.Height-height)/2 );
		glfwSetWindowTitle( _STRINGIZE(CFG_GLFW_WINDOW_TITLE) );
	}

#if CFG_OPENGL_INIT_EXTENSIONS
	Init_GL_Exts();
#endif

	if( _swapInterval>=0 ) glfwSwapInterval( _swapInterval );

	glfwEnable( GLFW_KEY_REPEAT );
	glfwDisable( GLFW_AUTO_POLL_EVENTS );
	glfwSetKeyCallback( OnKey );
	glfwSetCharCallback( OnChar );
	glfwSetMouseButtonCallback( OnMouseButton );
	glfwSetMousePosCallback( OnMousePos );
	glfwSetWindowCloseCallback(	OnWindowClose );
}

Array<BBGlfwVideoMode*> BBGlfwGame::GetGlfwVideoModes(){
	GLFWvidmode modes[1024];
	int n=glfwGetVideoModes( modes,1024 );
	Array<BBGlfwVideoMode*> bbmodes( n );
	for( int i=0;i<n;++i ){
		bbmodes[i]=new BBGlfwVideoMode( modes[i].Width,modes[i].Height,modes[i].RedBits,modes[i].GreenBits,modes[i].BlueBits );
	}
	return bbmodes;
}

BBGlfwVideoMode *BBGlfwGame::GetGlfwDesktopMode(){
	GLFWvidmode mode;
	glfwGetDesktopMode( &mode );
	return new BBGlfwVideoMode( mode.Width,mode.Height,mode.RedBits,mode.GreenBits,mode.BlueBits );
}

int BBGlfwGame::GetDeviceWidth(){
	int width,height;
	glfwGetWindowSize( &width,&height );
	return width;
}

int BBGlfwGame::GetDeviceHeight(){
	int width,height;
	glfwGetWindowSize( &width,&height );
	return height;
}

void BBGlfwGame::SetDeviceWindow( int width,int height,int flags ){

	SetGlfwWindow( width,height,8,8,8,0,CFG_OPENGL_DEPTH_BUFFER_ENABLED ? 32 : 0,0,(flags&1)!=0 );
}

Array<BBDisplayMode*> BBGlfwGame::GetDisplayModes(){

	GLFWvidmode vmodes[1024];
	int n=glfwGetVideoModes( vmodes,1024 );
	Array<BBDisplayMode*> modes( n );
	for( int i=0;i<n;++i ) modes[i]=new BBDisplayMode( vmodes[i].Width,vmodes[i].Height );
	return modes;
}

BBDisplayMode *BBGlfwGame::GetDesktopMode(){

	GLFWvidmode vmode;
	glfwGetDesktopMode( &vmode );
	return new BBDisplayMode( vmode.Width,vmode.Height );
}

void BBGlfwGame::SetSwapInterval( int interval ){
	_swapInterval=interval;
	if( _swapInterval>=0 ) glfwSwapInterval( _swapInterval );
}

void BBGlfwGame::UpdateEvents(){
	if( _suspended ){
		glfwWaitEvents();
	}else{
		glfwPollEvents();
	}
	if( glfwGetWindowParam( GLFW_ACTIVE ) ){
		if( _suspended ){
			ResumeGame();
			_nextUpdate=0;
		}
	}else if( glfwGetWindowParam( GLFW_ICONIFIED ) || CFG_MOJO_AUTO_SUSPEND_ENABLED ){
		if( !_suspended ){
			SuspendGame();
			_nextUpdate=0;
		}
	}
}

void BBGlfwGame::Run(){

#if	CFG_GLFW_WINDOW_WIDTH && CFG_GLFW_WINDOW_HEIGHT

	SetGlfwWindow( CFG_GLFW_WINDOW_WIDTH,CFG_GLFW_WINDOW_HEIGHT,8,8,8,0,CFG_OPENGL_DEPTH_BUFFER_ENABLED ? 32 : 0,0,CFG_GLFW_WINDOW_FULLSCREEN );

#endif

	StartGame();
	
	while( glfwGetWindowParam( GLFW_OPENED ) ){
	
		RenderGame();
		glfwSwapBuffers();
		
		if( _nextUpdate ){
			double delay=_nextUpdate-glfwGetTime();
			if( delay>0 ) glfwSleep( delay );
		}
		
		//Update user events
		UpdateEvents();

		//App suspended?		
		if( _suspended ) continue;

		//'Go nuts' mode!
		if( !_updateRate ){
			UpdateGame();
			continue;
		}
		
		//Reset update timer?
		if( !_nextUpdate ) _nextUpdate=glfwGetTime();
		
		//Catch up updates...
		int i=0;
		for( ;i<4;++i ){
		
			UpdateGame();
			if( !_nextUpdate ) break;
			
			_nextUpdate+=_updatePeriod;
			
			if( _nextUpdate>glfwGetTime() ) break;
		}
		
		if( i==4 ) _nextUpdate=0;
	}
}



//***** monkeygame.h *****

class BBMonkeyGame : public BBGlfwGame{
public:

	static void Main( int args,const char *argv[] );
};

//***** monkeygame.cpp *****

#define _QUOTE(X) #X
#define _STRINGIZE(X) _QUOTE(X)

void BBMonkeyGame::Main( int argc,const char *argv[] ){

	if( !glfwInit() ){
		puts( "glfwInit failed" );
		exit(-1);
	}

	BBMonkeyGame *game=new BBMonkeyGame();
	
	try{
	
		bb_std_main( argc,argv );
		
	}catch( ThrowableObject *ex ){
	
		glfwTerminate();
		
		game->Die( ex );
		
		return;
	}

	if( game->Delegate() ) game->Run();
	
	glfwTerminate();
}


// GLFW mojo runtime.
//
// Copyright 2011 Mark Sibly, all rights reserved.
// No warranty implied; use at your own risk.

//***** gxtkGraphics.h *****

class gxtkSurface;

class gxtkGraphics : public Object{
public:

	enum{
		MAX_VERTS=1024,
		MAX_QUADS=(MAX_VERTS/4)
	};

	int width;
	int height;

	int colorARGB;
	float r,g,b,alpha;
	float ix,iy,jx,jy,tx,ty;
	bool tformed;

	float vertices[MAX_VERTS*5];
	unsigned short quadIndices[MAX_QUADS*6];

	int primType;
	int vertCount;
	gxtkSurface *primSurf;
	
	gxtkGraphics();
	
	void Flush();
	float *Begin( int type,int count,gxtkSurface *surf );
	
	//***** GXTK API *****
	virtual int Width();
	virtual int Height();
	
	virtual int  BeginRender();
	virtual void EndRender();
	virtual void DiscardGraphics();

	virtual gxtkSurface *LoadSurface( String path );
	virtual gxtkSurface *LoadSurface__UNSAFE__( gxtkSurface *surface,String path );
	virtual gxtkSurface *CreateSurface( int width,int height );
	
	virtual int Cls( float r,float g,float b );
	virtual int SetAlpha( float alpha );
	virtual int SetColor( float r,float g,float b );
	virtual int SetBlend( int blend );
	virtual int SetScissor( int x,int y,int w,int h );
	virtual int SetMatrix( float ix,float iy,float jx,float jy,float tx,float ty );
	
	virtual int DrawPoint( float x,float y );
	virtual int DrawRect( float x,float y,float w,float h );
	virtual int DrawLine( float x1,float y1,float x2,float y2 );
	virtual int DrawOval( float x1,float y1,float x2,float y2 );
	virtual int DrawPoly( Array<Float> verts );
	virtual int DrawPoly2( Array<Float> verts,gxtkSurface *surface,int srcx,int srcy );
	virtual int DrawSurface( gxtkSurface *surface,float x,float y );
	virtual int DrawSurface2( gxtkSurface *surface,float x,float y,int srcx,int srcy,int srcw,int srch );
	
	virtual int ReadPixels( Array<int> pixels,int x,int y,int width,int height,int offset,int pitch );
	virtual int WritePixels2( gxtkSurface *surface,Array<int> pixels,int x,int y,int width,int height,int offset,int pitch );
};

class gxtkSurface : public Object{
public:
	unsigned char *data;
	int width;
	int height;
	int depth;
	int format;
	int seq;
	
	GLuint texture;
	float uscale;
	float vscale;
	
	gxtkSurface();
	
	void SetData( unsigned char *data,int width,int height,int depth );
	void SetSubData( int x,int y,int w,int h,unsigned *src,int pitch );
	void Bind();
	
	~gxtkSurface();
	
	//***** GXTK API *****
	virtual int Discard();
	virtual int Width();
	virtual int Height();
	virtual int Loaded();
	virtual bool OnUnsafeLoadComplete();
};

//***** gxtkGraphics.cpp *****

#ifndef GL_BGRA
#define GL_BGRA  0x80e1
#endif

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812f
#endif

#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif

static int Pow2Size( int n ){
	int i=1;
	while( i<n ) i+=i;
	return i;
}

gxtkGraphics::gxtkGraphics(){

	width=height=0;
#ifdef _glfw3_h_
	GLFWwindow *window=BBGlfwGame::GlfwGame()->GetGLFWwindow();
	if( window ) glfwGetWindowSize( BBGlfwGame::GlfwGame()->GetGLFWwindow(),&width,&height );
#else
	glfwGetWindowSize( &width,&height );
#endif
	
	if( CFG_OPENGL_GLES20_ENABLED ) return;
	
	for( int i=0;i<MAX_QUADS;++i ){
		quadIndices[i*6  ]=(short)(i*4);
		quadIndices[i*6+1]=(short)(i*4+1);
		quadIndices[i*6+2]=(short)(i*4+2);
		quadIndices[i*6+3]=(short)(i*4);
		quadIndices[i*6+4]=(short)(i*4+2);
		quadIndices[i*6+5]=(short)(i*4+3);
	}
}

void gxtkGraphics::Flush(){
	if( !vertCount ) return;

	if( primSurf ){
		glEnable( GL_TEXTURE_2D );
		primSurf->Bind();
	}
		
	switch( primType ){
	case 1:
		glDrawArrays( GL_POINTS,0,vertCount );
		break;
	case 2:
		glDrawArrays( GL_LINES,0,vertCount );
		break;
	case 3:
		glDrawArrays( GL_TRIANGLES,0,vertCount );
		break;
	case 4:
		glDrawElements( GL_TRIANGLES,vertCount/4*6,GL_UNSIGNED_SHORT,quadIndices );
		break;
	default:
		for( int j=0;j<vertCount;j+=primType ){
			glDrawArrays( GL_TRIANGLE_FAN,j,primType );
		}
		break;
	}

	if( primSurf ){
		glDisable( GL_TEXTURE_2D );
	}

	vertCount=0;
}

float *gxtkGraphics::Begin( int type,int count,gxtkSurface *surf ){
	if( primType!=type || primSurf!=surf || vertCount+count>MAX_VERTS ){
		Flush();
		primType=type;
		primSurf=surf;
	}
	float *vp=vertices+vertCount*5;
	vertCount+=count;
	return vp;
}

//***** GXTK API *****

int gxtkGraphics::Width(){
	return width;
}

int gxtkGraphics::Height(){
	return height;
}

int gxtkGraphics::BeginRender(){

	width=height=0;
#ifdef _glfw3_h_
	glfwGetWindowSize( BBGlfwGame::GlfwGame()->GetGLFWwindow(),&width,&height );
#else
	glfwGetWindowSize( &width,&height );
#endif
	
	if( CFG_OPENGL_GLES20_ENABLED ) return 0;
	
	glViewport( 0,0,width,height );

	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();
	glOrtho( 0,width,height,0,-1,1 );
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();
	
	glEnableClientState( GL_VERTEX_ARRAY );
	glVertexPointer( 2,GL_FLOAT,20,&vertices[0] );	
	
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 2,GL_FLOAT,20,&vertices[2] );
	
	glEnableClientState( GL_COLOR_ARRAY );
	glColorPointer( 4,GL_UNSIGNED_BYTE,20,&vertices[4] );
	
	glEnable( GL_BLEND );
	glBlendFunc( GL_ONE,GL_ONE_MINUS_SRC_ALPHA );
	
	glDisable( GL_TEXTURE_2D );
	
	vertCount=0;
	
	return 1;
}

void gxtkGraphics::EndRender(){
	if( !CFG_OPENGL_GLES20_ENABLED ) Flush();
}

void gxtkGraphics::DiscardGraphics(){
}

int gxtkGraphics::Cls( float r,float g,float b ){
	vertCount=0;

	glClearColor( r/255.0f,g/255.0f,b/255.0f,1 );
	glClear( GL_COLOR_BUFFER_BIT );

	return 0;
}

int gxtkGraphics::SetAlpha( float alpha ){
	this->alpha=alpha;
	
	int a=int(alpha*255);
	
	colorARGB=(a<<24) | (int(b*alpha)<<16) | (int(g*alpha)<<8) | int(r*alpha);
	
	return 0;
}

int gxtkGraphics::SetColor( float r,float g,float b ){
	this->r=r;
	this->g=g;
	this->b=b;

	int a=int(alpha*255);
	
	colorARGB=(a<<24) | (int(b*alpha)<<16) | (int(g*alpha)<<8) | int(r*alpha);
	
	return 0;
}

int gxtkGraphics::SetBlend( int blend ){

	Flush();
	
	switch( blend ){
	case 1:
		glBlendFunc( GL_ONE,GL_ONE );
		break;
	default:
		glBlendFunc( GL_ONE,GL_ONE_MINUS_SRC_ALPHA );
	}

	return 0;
}

int gxtkGraphics::SetScissor( int x,int y,int w,int h ){

	Flush();
	
	if( x!=0 || y!=0 || w!=Width() || h!=Height() ){
		glEnable( GL_SCISSOR_TEST );
		y=Height()-y-h;
		glScissor( x,y,w,h );
	}else{
		glDisable( GL_SCISSOR_TEST );
	}
	return 0;
}

int gxtkGraphics::SetMatrix( float ix,float iy,float jx,float jy,float tx,float ty ){

	tformed=(ix!=1 || iy!=0 || jx!=0 || jy!=1 || tx!=0 || ty!=0);

	this->ix=ix;this->iy=iy;this->jx=jx;this->jy=jy;this->tx=tx;this->ty=ty;

	return 0;
}

int gxtkGraphics::DrawPoint( float x,float y ){

	if( tformed ){
		float px=x;
		x=px * ix + y * jx + tx;
		y=px * iy + y * jy + ty;
	}
	
	float *vp=Begin( 1,1,0 );
	
	vp[0]=x;vp[1]=y;(int&)vp[4]=colorARGB;

	return 0;	
}
	
int gxtkGraphics::DrawLine( float x0,float y0,float x1,float y1 ){

	if( tformed ){
		float tx0=x0,tx1=x1;
		x0=tx0 * ix + y0 * jx + tx;y0=tx0 * iy + y0 * jy + ty;
		x1=tx1 * ix + y1 * jx + tx;y1=tx1 * iy + y1 * jy + ty;
	}
	
	float *vp=Begin( 2,2,0 );

	vp[0]=x0;vp[1]=y0;(int&)vp[4]=colorARGB;
	vp[5]=x1;vp[6]=y1;(int&)vp[9]=colorARGB;
	
	return 0;
}

int gxtkGraphics::DrawRect( float x,float y,float w,float h ){

	float x0=x,x1=x+w,x2=x+w,x3=x;
	float y0=y,y1=y,y2=y+h,y3=y+h;

	if( tformed ){
		float tx0=x0,tx1=x1,tx2=x2,tx3=x3;
		x0=tx0 * ix + y0 * jx + tx;y0=tx0 * iy + y0 * jy + ty;
		x1=tx1 * ix + y1 * jx + tx;y1=tx1 * iy + y1 * jy + ty;
		x2=tx2 * ix + y2 * jx + tx;y2=tx2 * iy + y2 * jy + ty;
		x3=tx3 * ix + y3 * jx + tx;y3=tx3 * iy + y3 * jy + ty;
	}
	
	float *vp=Begin( 4,4,0 );

	vp[0 ]=x0;vp[1 ]=y0;(int&)vp[4 ]=colorARGB;
	vp[5 ]=x1;vp[6 ]=y1;(int&)vp[9 ]=colorARGB;
	vp[10]=x2;vp[11]=y2;(int&)vp[14]=colorARGB;
	vp[15]=x3;vp[16]=y3;(int&)vp[19]=colorARGB;

	return 0;
}

int gxtkGraphics::DrawOval( float x,float y,float w,float h ){
	
	float xr=w/2.0f;
	float yr=h/2.0f;

	int n;
	if( tformed ){
		float dx_x=xr * ix;
		float dx_y=xr * iy;
		float dx=sqrtf( dx_x*dx_x+dx_y*dx_y );
		float dy_x=yr * jx;
		float dy_y=yr * jy;
		float dy=sqrtf( dy_x*dy_x+dy_y*dy_y );
		n=(int)( dx+dy );
	}else{
		n=(int)( abs( xr )+abs( yr ) );
	}
	
	if( n<12 ){
		n=12;
	}else if( n>MAX_VERTS ){
		n=MAX_VERTS;
	}else{
		n&=~3;
	}

	float x0=x+xr,y0=y+yr;
	
	float *vp=Begin( n,n,0 );

	for( int i=0;i<n;++i ){
	
		float th=i * 6.28318531f / n;

		float px=x0+cosf( th ) * xr;
		float py=y0-sinf( th ) * yr;
		
		if( tformed ){
			float ppx=px;
			px=ppx * ix + py * jx + tx;
			py=ppx * iy + py * jy + ty;
		}
		
		vp[0]=px;vp[1]=py;(int&)vp[4]=colorARGB;
		vp+=5;
	}
	
	return 0;
}

int gxtkGraphics::DrawPoly( Array<Float> verts ){

	int n=verts.Length()/2;
	if( n<1 || n>MAX_VERTS ) return 0;
	
	float *vp=Begin( n,n,0 );
	
	for( int i=0;i<n;++i ){
		int j=i*2;
		if( tformed ){
			vp[0]=verts[j] * ix + verts[j+1] * jx + tx;
			vp[1]=verts[j] * iy + verts[j+1] * jy + ty;
		}else{
			vp[0]=verts[j];
			vp[1]=verts[j+1];
		}
		(int&)vp[4]=colorARGB;
		vp+=5;
	}

	return 0;
}

int gxtkGraphics::DrawPoly2( Array<Float> verts,gxtkSurface *surface,int srcx,int srcy ){

	int n=verts.Length()/4;
	if( n<1 || n>MAX_VERTS ) return 0;
		
	float *vp=Begin( n,n,surface );
	
	for( int i=0;i<n;++i ){
		int j=i*4;
		if( tformed ){
			vp[0]=verts[j] * ix + verts[j+1] * jx + tx;
			vp[1]=verts[j] * iy + verts[j+1] * jy + ty;
		}else{
			vp[0]=verts[j];
			vp[1]=verts[j+1];
		}
		vp[2]=(srcx+verts[j+2])*surface->uscale;
		vp[3]=(srcy+verts[j+3])*surface->vscale;
		(int&)vp[4]=colorARGB;
		vp+=5;
	}
	
	return 0;
}

int gxtkGraphics::DrawSurface( gxtkSurface *surf,float x,float y ){
	
	float w=surf->Width();
	float h=surf->Height();
	float x0=x,x1=x+w,x2=x+w,x3=x;
	float y0=y,y1=y,y2=y+h,y3=y+h;
	float u0=0,u1=w*surf->uscale;
	float v0=0,v1=h*surf->vscale;

	if( tformed ){
		float tx0=x0,tx1=x1,tx2=x2,tx3=x3;
		x0=tx0 * ix + y0 * jx + tx;y0=tx0 * iy + y0 * jy + ty;
		x1=tx1 * ix + y1 * jx + tx;y1=tx1 * iy + y1 * jy + ty;
		x2=tx2 * ix + y2 * jx + tx;y2=tx2 * iy + y2 * jy + ty;
		x3=tx3 * ix + y3 * jx + tx;y3=tx3 * iy + y3 * jy + ty;
	}
	
	float *vp=Begin( 4,4,surf );
	
	vp[0 ]=x0;vp[1 ]=y0;vp[2 ]=u0;vp[3 ]=v0;(int&)vp[4 ]=colorARGB;
	vp[5 ]=x1;vp[6 ]=y1;vp[7 ]=u1;vp[8 ]=v0;(int&)vp[9 ]=colorARGB;
	vp[10]=x2;vp[11]=y2;vp[12]=u1;vp[13]=v1;(int&)vp[14]=colorARGB;
	vp[15]=x3;vp[16]=y3;vp[17]=u0;vp[18]=v1;(int&)vp[19]=colorARGB;
	
	return 0;
}

int gxtkGraphics::DrawSurface2( gxtkSurface *surf,float x,float y,int srcx,int srcy,int srcw,int srch ){
	
	float w=srcw;
	float h=srch;
	float x0=x,x1=x+w,x2=x+w,x3=x;
	float y0=y,y1=y,y2=y+h,y3=y+h;
	float u0=srcx*surf->uscale,u1=(srcx+srcw)*surf->uscale;
	float v0=srcy*surf->vscale,v1=(srcy+srch)*surf->vscale;

	if( tformed ){
		float tx0=x0,tx1=x1,tx2=x2,tx3=x3;
		x0=tx0 * ix + y0 * jx + tx;y0=tx0 * iy + y0 * jy + ty;
		x1=tx1 * ix + y1 * jx + tx;y1=tx1 * iy + y1 * jy + ty;
		x2=tx2 * ix + y2 * jx + tx;y2=tx2 * iy + y2 * jy + ty;
		x3=tx3 * ix + y3 * jx + tx;y3=tx3 * iy + y3 * jy + ty;
	}
	
	float *vp=Begin( 4,4,surf );
	
	vp[0 ]=x0;vp[1 ]=y0;vp[2 ]=u0;vp[3 ]=v0;(int&)vp[4 ]=colorARGB;
	vp[5 ]=x1;vp[6 ]=y1;vp[7 ]=u1;vp[8 ]=v0;(int&)vp[9 ]=colorARGB;
	vp[10]=x2;vp[11]=y2;vp[12]=u1;vp[13]=v1;(int&)vp[14]=colorARGB;
	vp[15]=x3;vp[16]=y3;vp[17]=u0;vp[18]=v1;(int&)vp[19]=colorARGB;
	
	return 0;
}
	
int gxtkGraphics::ReadPixels( Array<int> pixels,int x,int y,int width,int height,int offset,int pitch ){

	Flush();

	unsigned *p=(unsigned*)malloc(width*height*4);

	glReadPixels( x,this->height-y-height,width,height,GL_BGRA,GL_UNSIGNED_BYTE,p );
	
	for( int py=0;py<height;++py ){
		memcpy( &pixels[offset+py*pitch],&p[(height-py-1)*width],width*4 );
	}
	
	free( p );
	
	return 0;
}

int gxtkGraphics::WritePixels2( gxtkSurface *surface,Array<int> pixels,int x,int y,int width,int height,int offset,int pitch ){

	Flush();
	
	surface->SetSubData( x,y,width,height,(unsigned*)&pixels[offset],pitch );
	
	return 0;
}

//***** gxtkSurface *****

gxtkSurface::gxtkSurface():data(0),width(0),height(0),depth(0),format(0),seq(-1),texture(0),uscale(0),vscale(0){
}

gxtkSurface::~gxtkSurface(){
	Discard();
}

int gxtkSurface::Discard(){
	if( seq==glfwGraphicsSeq ){
		glDeleteTextures( 1,&texture );
		seq=-1;
	}
	if( data ){
		free( data );
		data=0;
	}
	return 0;
}

int gxtkSurface::Width(){
	return width;
}

int gxtkSurface::Height(){
	return height;
}

int gxtkSurface::Loaded(){
	return 1;
}

//Careful! Can't call any GL here as it may be executing off-thread.
//
void gxtkSurface::SetData( unsigned char *data,int width,int height,int depth ){

	this->data=data;
	this->width=width;
	this->height=height;
	this->depth=depth;
	
	unsigned char *p=data;
	int n=width*height;
	
	switch( depth ){
	case 1:
		format=GL_LUMINANCE;
		break;
	case 2:
		format=GL_LUMINANCE_ALPHA;
		if( data ){
			while( n-- ){	//premultiply alpha
				p[0]=p[0]*p[1]/255;
				p+=2;
			}
		}
		break;
	case 3:
		format=GL_RGB;
		break;
	case 4:
		format=GL_RGBA;
		if( data ){
			while( n-- ){	//premultiply alpha
				p[0]=p[0]*p[3]/255;
				p[1]=p[1]*p[3]/255;
				p[2]=p[2]*p[3]/255;
				p+=4;
			}
		}
		break;
	}
}

void gxtkSurface::SetSubData( int x,int y,int w,int h,unsigned *src,int pitch ){
	if( format!=GL_RGBA ) return;
	
	if( !data ) data=(unsigned char*)malloc( width*height*4 );
	
	unsigned *dst=(unsigned*)data+y*width+x;
	
	for( int py=0;py<h;++py ){
		unsigned *d=dst+py*width;
		unsigned *s=src+py*pitch;
		for( int px=0;px<w;++px ){
			unsigned p=*s++;
			unsigned a=p>>24;
			*d++=(a<<24) | ((p>>0&0xff)*a/255<<16) | ((p>>8&0xff)*a/255<<8) | ((p>>16&0xff)*a/255);
		}
	}
	
	if( seq==glfwGraphicsSeq ){
		glBindTexture( GL_TEXTURE_2D,texture );
		glPixelStorei( GL_UNPACK_ALIGNMENT,1 );
		if( width==pitch ){
			glTexSubImage2D( GL_TEXTURE_2D,0,x,y,w,h,format,GL_UNSIGNED_BYTE,dst );
		}else{
			for( int py=0;py<h;++py ){
				glTexSubImage2D( GL_TEXTURE_2D,0,x,y+py,w,1,format,GL_UNSIGNED_BYTE,dst+py*width );
			}
		}
	}
}

void gxtkSurface::Bind(){

	if( !glfwGraphicsSeq ) return;
	
	if( seq==glfwGraphicsSeq ){
		glBindTexture( GL_TEXTURE_2D,texture );
		return;
	}
	
	seq=glfwGraphicsSeq;
	
	glGenTextures( 1,&texture );
	glBindTexture( GL_TEXTURE_2D,texture );
	
	if( CFG_MOJO_IMAGE_FILTERING_ENABLED ){
		glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR );
	}else{
		glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST );
	}

	glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE );

	int texwidth=width;
	int texheight=height;
	
	glTexImage2D( GL_TEXTURE_2D,0,format,texwidth,texheight,0,format,GL_UNSIGNED_BYTE,0 );
	if( glGetError()!=GL_NO_ERROR ){
		texwidth=Pow2Size( width );
		texheight=Pow2Size( height );
		glTexImage2D( GL_TEXTURE_2D,0,format,texwidth,texheight,0,format,GL_UNSIGNED_BYTE,0 );
	}
	
	uscale=1.0/texwidth;
	vscale=1.0/texheight;
	
	if( data ){
		glPixelStorei( GL_UNPACK_ALIGNMENT,1 );
		glTexSubImage2D( GL_TEXTURE_2D,0,0,0,width,height,format,GL_UNSIGNED_BYTE,data );
	}
}

bool gxtkSurface::OnUnsafeLoadComplete(){
	Bind();
	return true;
}

gxtkSurface *gxtkGraphics::LoadSurface__UNSAFE__( gxtkSurface *surface,String path ){
	int width,height,depth;
	unsigned char *data=BBGlfwGame::GlfwGame()->LoadImageData( path,&width,&height,&depth );
	if( !data ) return 0;
	surface->SetData( data,width,height,depth );
	return surface;
}

gxtkSurface *gxtkGraphics::LoadSurface( String path ){
	gxtkSurface *surf=LoadSurface__UNSAFE__( new gxtkSurface(),path );
	if( !surf ) return 0;
	surf->Bind();
	return surf;
}

gxtkSurface *gxtkGraphics::CreateSurface( int width,int height ){
	gxtkSurface *surf=new gxtkSurface();
	surf->SetData( 0,width,height,4 );
	surf->Bind();
	return surf;
}

//***** gxtkAudio.h *****

class gxtkSample;

class gxtkChannel{
public:
	ALuint source;
	gxtkSample *sample;
	int flags;
	int state;
	
	int AL_Source();
};

class gxtkAudio : public Object{
public:
	static gxtkAudio *audio;
	
	ALCdevice *alcDevice;
	ALCcontext *alcContext;
	gxtkChannel channels[33];

	gxtkAudio();

	virtual void mark();

	//***** GXTK API *****
	virtual int Suspend();
	virtual int Resume();

	virtual gxtkSample *LoadSample__UNSAFE__( gxtkSample *sample,String path );
	virtual gxtkSample *LoadSample( String path );
	virtual int PlaySample( gxtkSample *sample,int channel,int flags );

	virtual int StopChannel( int channel );
	virtual int PauseChannel( int channel );
	virtual int ResumeChannel( int channel );
	virtual int ChannelState( int channel );
	virtual int SetVolume( int channel,float volume );
	virtual int SetPan( int channel,float pan );
	virtual int SetRate( int channel,float rate );
	
	virtual int PlayMusic( String path,int flags );
	virtual int StopMusic();
	virtual int PauseMusic();
	virtual int ResumeMusic();
	virtual int MusicState();
	virtual int SetMusicVolume( float volume );
};

class gxtkSample : public Object{
public:
	ALuint al_buffer;

	gxtkSample();
	gxtkSample( ALuint buf );
	~gxtkSample();
	
	void SetBuffer( ALuint buf );
	
	//***** GXTK API *****
	virtual int Discard();
};

//***** gxtkAudio.cpp *****

gxtkAudio *gxtkAudio::audio;

static std::vector<ALuint> discarded;

static void FlushDiscarded(){

	if( !discarded.size() ) return;
	
	for( int i=0;i<33;++i ){
		gxtkChannel *chan=&gxtkAudio::audio->channels[i];
		if( chan->state ){
			int state=0;
			alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
			if( state==AL_STOPPED ) alSourcei( chan->source,AL_BUFFER,0 );
		}
	}
	
	std::vector<ALuint> out;
	
	for( int i=0;i<discarded.size();++i ){
		ALuint buf=discarded[i];
		alDeleteBuffers( 1,&buf );
		ALenum err=alGetError();
		if( err==AL_NO_ERROR ){
//			printf( "alDeleteBuffers OK!\n" );fflush( stdout );
		}else{
//			printf( "alDeleteBuffers failed...\n" );fflush( stdout );
			out.push_back( buf );
		}
	}
	discarded=out;
}

int gxtkChannel::AL_Source(){
	if( source ) return source;

	/*	
	static int n;
	if( ++n<17 ){
		alGetError();
		alGenSources( 1,&source );
		if( alGetError()==AL_NO_ERROR ) return source;
	}
	*/
	
	alGetError();
	alGenSources( 1,&source );
	if( alGetError()==AL_NO_ERROR ) return source;
	
	//couldn't create source...steal a free source...?
	//
	source=0;
	for( int i=0;i<32;++i ){
		gxtkChannel *chan=&gxtkAudio::audio->channels[i];
		if( !chan->source || gxtkAudio::audio->ChannelState( i ) ) continue;
//		puts( "Stealing source!" );
		source=chan->source;
		chan->source=0;
		break;
	}
	return source;
}

gxtkAudio::gxtkAudio(){

	audio=this;

	if( alcDevice=alcOpenDevice( 0 ) ){
		if( alcContext=alcCreateContext( alcDevice,0 ) ){
			if( alcMakeContextCurrent( alcContext ) ){
				//alc all go!
			}else{
				bbPrint( "OpenAl error: alcMakeContextCurrent failed" );
			}
		}else{
			bbPrint( "OpenAl error: alcCreateContext failed" );
		}
	}else{
		bbPrint( "OpenAl error: alcOpenDevice failed" );
	}

	alDistanceModel( AL_NONE );
	
	memset( channels,0,sizeof(channels) );

	channels[32].AL_Source();
}

void gxtkAudio::mark(){
	for( int i=0;i<33;++i ){
		gxtkChannel *chan=&channels[i];
		if( chan->state!=0 ){
			int state=0;
			alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
			if( state!=AL_STOPPED ) gc_mark( chan->sample );
		}
	}
}

int gxtkAudio::Suspend(){
	for( int i=0;i<33;++i ){
		gxtkChannel *chan=&channels[i];
		if( chan->state==1 ){
			int state=0;
			alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
			if( state==AL_PLAYING ) alSourcePause( chan->source );
		}
	}
	return 0;
}

int gxtkAudio::Resume(){
	for( int i=0;i<33;++i ){
		gxtkChannel *chan=&channels[i];
		if( chan->state==1 ){
			int state=0;
			alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
			if( state==AL_PAUSED ) alSourcePlay( chan->source );
		}
	}
	return 0;
}

gxtkSample *gxtkAudio::LoadSample__UNSAFE__( gxtkSample *sample,String path ){

	int length=0;
	int channels=0;
	int format=0;
	int hertz=0;
	unsigned char *data=BBGlfwGame::GlfwGame()->LoadAudioData( path,&length,&channels,&format,&hertz );
	if( !data ) return 0;
	
	int al_format=0;
	if( format==1 && channels==1 ){
		al_format=AL_FORMAT_MONO8;
	}else if( format==1 && channels==2 ){
		al_format=AL_FORMAT_STEREO8;
	}else if( format==2 && channels==1 ){
		al_format=AL_FORMAT_MONO16;
	}else if( format==2 && channels==2 ){
		al_format=AL_FORMAT_STEREO16;
	}
	
	int size=length*channels*format;
	
	ALuint al_buffer;
	alGenBuffers( 1,&al_buffer );
	alBufferData( al_buffer,al_format,data,size,hertz );
	free( data );
	
	sample->SetBuffer( al_buffer );
	return sample;
}

gxtkSample *gxtkAudio::LoadSample( String path ){

	FlushDiscarded();

	return LoadSample__UNSAFE__( new gxtkSample(),path );
}

int gxtkAudio::PlaySample( gxtkSample *sample,int channel,int flags ){

	FlushDiscarded();
	
	gxtkChannel *chan=&channels[channel];
	
	if( !chan->AL_Source() ) return -1;
	
	alSourceStop( chan->source );
	alSourcei( chan->source,AL_BUFFER,sample->al_buffer );
	alSourcei( chan->source,AL_LOOPING,flags ? 1 : 0 );
	alSourcePlay( chan->source );
	
	gc_assign( chan->sample,sample );

	chan->flags=flags;
	chan->state=1;

	return 0;
}

int gxtkAudio::StopChannel( int channel ){
	gxtkChannel *chan=&channels[channel];

	if( chan->state!=0 ){
		alSourceStop( chan->source );
		chan->state=0;
	}
	return 0;
}

int gxtkAudio::PauseChannel( int channel ){
	gxtkChannel *chan=&channels[channel];

	if( chan->state==1 ){
		int state=0;
		alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
		if( state==AL_STOPPED ){
			chan->state=0;
		}else{
			alSourcePause( chan->source );
			chan->state=2;
		}
	}
	return 0;
}

int gxtkAudio::ResumeChannel( int channel ){
	gxtkChannel *chan=&channels[channel];

	if( chan->state==2 ){
		alSourcePlay( chan->source );
		chan->state=1;
	}
	return 0;
}

int gxtkAudio::ChannelState( int channel ){
	gxtkChannel *chan=&channels[channel];
	
	if( chan->state==1 ){
		int state=0;
		alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
		if( state==AL_STOPPED ) chan->state=0;
	}
	return chan->state;
}

int gxtkAudio::SetVolume( int channel,float volume ){
	gxtkChannel *chan=&channels[channel];

	alSourcef( chan->AL_Source(),AL_GAIN,volume );
	return 0;
}

int gxtkAudio::SetPan( int channel,float pan ){
	gxtkChannel *chan=&channels[channel];
	
	float x=sinf( pan ),y=0,z=-cosf( pan );
	alSource3f( chan->AL_Source(),AL_POSITION,x,y,z );
	return 0;
}

int gxtkAudio::SetRate( int channel,float rate ){
	gxtkChannel *chan=&channels[channel];

	alSourcef( chan->AL_Source(),AL_PITCH,rate );
	return 0;
}

int gxtkAudio::PlayMusic( String path,int flags ){
	StopMusic();
	
	gxtkSample *music=LoadSample( path );
	if( !music ) return -1;
	
	PlaySample( music,32,flags );
	return 0;
}

int gxtkAudio::StopMusic(){
	StopChannel( 32 );
	return 0;
}

int gxtkAudio::PauseMusic(){
	PauseChannel( 32 );
	return 0;
}

int gxtkAudio::ResumeMusic(){
	ResumeChannel( 32 );
	return 0;
}

int gxtkAudio::MusicState(){
	return ChannelState( 32 );
}

int gxtkAudio::SetMusicVolume( float volume ){
	SetVolume( 32,volume );
	return 0;
}

gxtkSample::gxtkSample():
al_buffer(0){
}

gxtkSample::gxtkSample( ALuint buf ):
al_buffer(buf){
}

gxtkSample::~gxtkSample(){
	puts( "Discarding sample" );
	Discard();
}

void gxtkSample::SetBuffer( ALuint buf ){
	al_buffer=buf;
}

int gxtkSample::Discard(){
	if( al_buffer ){
		discarded.push_back( al_buffer );
		al_buffer=0;
	}
	return 0;
}


// ***** thread.h *****

#if __cplusplus_winrt

using namespace Windows::System::Threading;

#endif

class BBThread : public Object{
public:
	Object *result;
	
	BBThread();
	~BBThread();
	
	virtual void Start();
	virtual bool IsRunning();
	virtual Object *Result();
	virtual void SetResult( Object *result );
	
	virtual void Run__UNSAFE__();
	
	virtual void Wait();
	
private:

	enum{
		INIT=0,
		RUNNING=1,
		FINISHED=2
	};

	
	int _state;
	Object *_result;
	
#if __cplusplus_winrt

	friend class Launcher;

	class Launcher{
	
		friend class BBThread;
		BBThread *_thread;
		
		Launcher( BBThread *thread ):_thread(thread){
		}
		
		public:
		void operator()( IAsyncAction ^operation ){
			_thread->Run__UNSAFE__();
			_thread->_state=FINISHED;
		} 
	};

#elif _WIN32

	DWORD _id;
	HANDLE _handle;
	
	static DWORD WINAPI run( void *p );
	
#else

	pthread_t _handle;
	
	static void *run( void *p );
	
#endif

};

// ***** thread.cpp *****

BBThread::BBThread():_result( 0 ),_state( INIT ){
}

BBThread::~BBThread(){
	Wait();
}

bool BBThread::IsRunning(){
	return _state==RUNNING;
}

void BBThread::SetResult( Object *result ){
	_result=result;
}

Object *BBThread::Result(){
	return _result;
}

void BBThread::Run__UNSAFE__(){
}

#if __cplusplus_winrt

void BBThread::Start(){
	if( _state==RUNNING ) return;
	
	if( _state==FINISHED ) {}

	_result=0;
	
	_state=RUNNING;
	
	Launcher launcher( this );
	
	auto handler=ref new WorkItemHandler( launcher );
	
	ThreadPool::RunAsync( handler );
}

void BBThread::Wait(){
//	exit( -1 );
}

#elif _WIN32

void BBThread::Start(){
	if( _state==RUNNING ) return;
	
	if( _state==FINISHED ) CloseHandle( _handle );

	_state=RUNNING;

	_handle=CreateThread( 0,0,run,this,0,&_id );
	
//	_handle=CreateThread( 0,0,run,this,CREATE_SUSPENDED,&_id );
//	SetThreadPriority( _handle,THREAD_PRIORITY_ABOVE_NORMAL );
//	ResumeThread( _handle );
}

void BBThread::Wait(){
	if( _state==INIT ) return;

	WaitForSingleObject( _handle,INFINITE );
	CloseHandle( _handle );

	_state=INIT;
}

DWORD WINAPI BBThread::run( void *p ){
	BBThread *thread=(BBThread*)p;

	thread->Run__UNSAFE__();
	
	thread->_state=FINISHED;
	return 0;
}

#else

void BBThread::Start(){
	if( _state==RUNNING ) return;
	
	if( _state==FINISHED ) pthread_join( _handle,0 );

	_result=0;
		
	_state=RUNNING;
	
	pthread_create( &_handle,0,run,this );
}

void BBThread::Wait(){
	if( _state==INIT ) return;
	
	pthread_join( _handle,0 );
	
	_state=INIT;
}

void *BBThread::run( void *p ){
	BBThread *thread=(BBThread*)p;

	thread->Run__UNSAFE__();

	thread->_state=FINISHED;
	return 0;
}

#endif

class c_App;
class c_Game;
class c_GameDelegate;
class c_Image;
class c_GraphicsContext;
class c_Frame;
class c_InputDevice;
class c_JoyState;
class c_DisplayMode;
class c_Map;
class c_IntMap;
class c_Stack;
class c_Node;
class c_BBGameEvent;
class c_GameObject;
class c_Button;
class c_VolumeSlider;
class c_Crosshair;
class c_Player;
class c_Animation;
class c_Sound;
class c_RoomMap;
class c_WeaponInterface;
class c_Interactables;
class c_Room;
class c_List;
class c_Node2;
class c_HeadNode;
class c_List2;
class c_Node3;
class c_HeadNode2;
class c_Enemy;
class c_List3;
class c_Node4;
class c_HeadNode3;
class c_Weapon;
class c_Sword;
class c_Vec2D;
class c_List4;
class c_Node5;
class c_HeadNode4;
class c_Enumerator;
class c_Enumerator2;
class c_Item;
class c_HealthPotion;
class c_Enumerator3;
class c_Enumerator4;
class c_App : public Object{
	public:
	c_App();
	c_App* m_new();
	int p_OnResize();
	virtual int p_OnCreate();
	int p_OnSuspend();
	int p_OnResume();
	virtual int p_OnUpdate();
	int p_OnLoading();
	virtual int p_OnRender();
	int p_OnClose();
	int p_OnBack();
	void mark();
	String debug();
};
String dbg_type(c_App**p){return "App";}
class c_Game : public c_App{
	public:
	c_Image* m_menuBackground;
	c_Button* m_menuBut;
	c_Button* m_optionsBut;
	c_Button* m_exitBut;
	c_Button* m_playBut;
	Array<c_Button* > m_menuArray;
	Array<c_Button* > m_optionsArray;
	Array<c_Button* > m_deadArray;
	c_VolumeSlider* m_effectsVol;
	c_VolumeSlider* m_musicVol;
	Array<c_VolumeSlider* > m_optionsSliders;
	c_Crosshair* m_xhair;
	c_Player* m_p1;
	c_RoomMap* m_Room1;
	c_RoomMap* m_Room2;
	c_RoomMap* m_Room3;
	String m_gameState;
	int m_butIndex;
	int m_optIndex;
	c_Game();
	c_Game* m_new();
	int p_OnCreate();
	int p_OnUpdate();
	int p_OnRender();
	void mark();
	String debug();
};
String dbg_type(c_Game**p){return "Game";}
extern c_App* bb_app__app;
class c_GameDelegate : public BBGameDelegate{
	public:
	gxtkGraphics* m__graphics;
	gxtkAudio* m__audio;
	c_InputDevice* m__input;
	c_GameDelegate();
	c_GameDelegate* m_new();
	void StartGame();
	void SuspendGame();
	void ResumeGame();
	void UpdateGame();
	void RenderGame();
	void KeyEvent(int,int);
	void MouseEvent(int,int,Float,Float);
	void TouchEvent(int,int,Float,Float);
	void MotionEvent(int,int,Float,Float,Float);
	void DiscardGraphics();
	void mark();
	String debug();
};
String dbg_type(c_GameDelegate**p){return "GameDelegate";}
extern c_GameDelegate* bb_app__delegate;
extern BBGame* bb_app__game;
extern c_Game* bb_Ragnarok_game;
int bbMain();
extern gxtkGraphics* bb_graphics_device;
int bb_graphics_SetGraphicsDevice(gxtkGraphics*);
class c_Image : public Object{
	public:
	gxtkSurface* m_surface;
	int m_width;
	int m_height;
	Array<c_Frame* > m_frames;
	int m_flags;
	Float m_tx;
	Float m_ty;
	c_Image* m_source;
	c_Image();
	static int m_DefaultFlags;
	c_Image* m_new();
	int p_SetHandle(Float,Float);
	int p_ApplyFlags(int);
	c_Image* p_Init(gxtkSurface*,int,int);
	c_Image* p_Init2(gxtkSurface*,int,int,int,int,int,int,c_Image*,int,int,int,int);
	c_Image* p_GrabImage(int,int,int,int,int,int);
	int p_Width();
	int p_Height();
	int p_Frames();
	void mark();
	String debug();
};
String dbg_type(c_Image**p){return "Image";}
class c_GraphicsContext : public Object{
	public:
	c_Image* m_defaultFont;
	c_Image* m_font;
	int m_firstChar;
	int m_matrixSp;
	Float m_ix;
	Float m_iy;
	Float m_jx;
	Float m_jy;
	Float m_tx;
	Float m_ty;
	int m_tformed;
	int m_matDirty;
	Float m_color_r;
	Float m_color_g;
	Float m_color_b;
	Float m_alpha;
	int m_blend;
	Float m_scissor_x;
	Float m_scissor_y;
	Float m_scissor_width;
	Float m_scissor_height;
	Array<Float > m_matrixStack;
	c_GraphicsContext();
	c_GraphicsContext* m_new();
	int p_Validate();
	void mark();
	String debug();
};
String dbg_type(c_GraphicsContext**p){return "GraphicsContext";}
extern c_GraphicsContext* bb_graphics_context;
String bb_data_FixDataPath(String);
class c_Frame : public Object{
	public:
	int m_x;
	int m_y;
	c_Frame();
	c_Frame* m_new(int,int);
	c_Frame* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Frame**p){return "Frame";}
c_Image* bb_graphics_LoadImage(String,int,int);
c_Image* bb_graphics_LoadImage2(String,int,int,int,int);
int bb_graphics_SetFont(c_Image*,int);
extern gxtkAudio* bb_audio_device;
int bb_audio_SetAudioDevice(gxtkAudio*);
class c_InputDevice : public Object{
	public:
	Array<c_JoyState* > m__joyStates;
	Array<bool > m__keyDown;
	int m__keyHitPut;
	Array<int > m__keyHitQueue;
	Array<int > m__keyHit;
	int m__charGet;
	int m__charPut;
	Array<int > m__charQueue;
	Float m__mouseX;
	Float m__mouseY;
	Array<Float > m__touchX;
	Array<Float > m__touchY;
	Float m__accelX;
	Float m__accelY;
	Float m__accelZ;
	c_InputDevice();
	c_InputDevice* m_new();
	void p_PutKeyHit(int);
	void p_BeginUpdate();
	void p_EndUpdate();
	void p_KeyEvent(int,int);
	void p_MouseEvent(int,int,Float,Float);
	void p_TouchEvent(int,int,Float,Float);
	void p_MotionEvent(int,int,Float,Float,Float);
	int p_KeyHit(int);
	bool p_KeyDown(int);
	Float p_MouseX();
	Float p_MouseY();
	void mark();
	String debug();
};
String dbg_type(c_InputDevice**p){return "InputDevice";}
class c_JoyState : public Object{
	public:
	Array<Float > m_joyx;
	Array<Float > m_joyy;
	Array<Float > m_joyz;
	Array<bool > m_buttons;
	c_JoyState();
	c_JoyState* m_new();
	void mark();
	String debug();
};
String dbg_type(c_JoyState**p){return "JoyState";}
extern c_InputDevice* bb_input_device;
int bb_input_SetInputDevice(c_InputDevice*);
extern int bb_app__devWidth;
extern int bb_app__devHeight;
void bb_app_ValidateDeviceWindow(bool);
class c_DisplayMode : public Object{
	public:
	int m__width;
	int m__height;
	c_DisplayMode();
	c_DisplayMode* m_new(int,int);
	c_DisplayMode* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_DisplayMode**p){return "DisplayMode";}
class c_Map : public Object{
	public:
	c_Node* m_root;
	c_Map();
	c_Map* m_new();
	virtual int p_Compare(int,int)=0;
	c_Node* p_FindNode(int);
	bool p_Contains(int);
	int p_RotateLeft(c_Node*);
	int p_RotateRight(c_Node*);
	int p_InsertFixup(c_Node*);
	bool p_Set(int,c_DisplayMode*);
	bool p_Insert(int,c_DisplayMode*);
	void mark();
	String debug();
};
String dbg_type(c_Map**p){return "Map";}
class c_IntMap : public c_Map{
	public:
	c_IntMap();
	c_IntMap* m_new();
	int p_Compare(int,int);
	void mark();
	String debug();
};
String dbg_type(c_IntMap**p){return "IntMap";}
class c_Stack : public Object{
	public:
	Array<c_DisplayMode* > m_data;
	int m_length;
	c_Stack();
	c_Stack* m_new();
	c_Stack* m_new2(Array<c_DisplayMode* >);
	void p_Push(c_DisplayMode*);
	void p_Push2(Array<c_DisplayMode* >,int,int);
	void p_Push3(Array<c_DisplayMode* >,int);
	Array<c_DisplayMode* > p_ToArray();
	void mark();
	String debug();
};
String dbg_type(c_Stack**p){return "Stack";}
class c_Node : public Object{
	public:
	int m_key;
	c_Node* m_right;
	c_Node* m_left;
	c_DisplayMode* m_value;
	int m_color;
	c_Node* m_parent;
	c_Node();
	c_Node* m_new(int,c_DisplayMode*,int,c_Node*);
	c_Node* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Node**p){return "Node";}
extern Array<c_DisplayMode* > bb_app__displayModes;
extern c_DisplayMode* bb_app__desktopMode;
int bb_app_DeviceWidth();
int bb_app_DeviceHeight();
void bb_app_EnumDisplayModes();
extern gxtkGraphics* bb_graphics_renderDevice;
int bb_graphics_SetMatrix(Float,Float,Float,Float,Float,Float);
int bb_graphics_SetMatrix2(Array<Float >);
int bb_graphics_SetColor(Float,Float,Float);
int bb_graphics_SetAlpha(Float);
int bb_graphics_SetBlend(int);
int bb_graphics_SetScissor(Float,Float,Float,Float);
int bb_graphics_BeginRender();
int bb_graphics_EndRender();
class c_BBGameEvent : public Object{
	public:
	c_BBGameEvent();
	void mark();
	String debug();
};
String dbg_type(c_BBGameEvent**p){return "BBGameEvent";}
void bb_app_EndApp();
void bb_app_SetDeviceWindow(int,int,int);
extern int bb_app__updateRate;
void bb_app_SetUpdateRate(int);
int bb_audio_SetMusicVolume(Float);
class c_GameObject : public Object{
	public:
	Float m_x;
	Float m_y;
	int m_width;
	int m_height;
	String m_path;
	c_Image* m_sprite;
	c_GameObject();
	c_GameObject* m_new(Float,Float,int,int,String);
	c_GameObject* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_GameObject**p){return "GameObject";}
class c_Button : public c_GameObject{
	public:
	String m_action;
	bool m_active;
	c_Button();
	c_Button* m_new(String);
	c_Button* m_new2();
	int p_SetAction(String);
	int p_SetActive(bool);
	String p_GetAction();
	int p_Draw(int,int);
	void mark();
	String debug();
};
String dbg_type(c_Button**p){return "Button";}
class c_VolumeSlider : public c_GameObject{
	public:
	int m_dialNum;
	Float m_volPercent;
	c_VolumeSlider();
	c_VolumeSlider* m_new(int,int,int,int,int);
	c_VolumeSlider* m_new2();
	int p_updateVol();
	Float p_getVol();
	int p_Draw2();
	void mark();
	String debug();
};
String dbg_type(c_VolumeSlider**p){return "VolumeSlider";}
int bb_audio_PlayMusic(String,int);
class c_Crosshair : public c_GameObject{
	public:
	c_Crosshair();
	c_Crosshair* m_new(int);
	c_Crosshair* m_new2();
	int p_Draw2();
	void mark();
	String debug();
};
String dbg_type(c_Crosshair**p){return "Crosshair";}
class c_Player : public c_GameObject{
	public:
	int m_speed;
	c_Animation* m_idleR;
	c_Animation* m_idleL;
	c_Animation* m_right;
	c_Animation* m_left;
	c_Image* m_heartEmptySprite;
	c_Image* m_heartFullSprite;
	int m_maxHealth;
	int m_curHealth;
	c_Sound* m_hurtSound;
	c_WeaponInterface* m_curWeapon;
	Array<c_Interactables* > m_itemArray;
	Float m_centreX;
	Float m_centreY;
	bool m_uber;
	int m_uberCD;
	int m_uberTime;
	c_Vec2D* m_movementVec;
	String m_animState;
	c_Player();
	c_Player* m_new(Float,Float,int,int,int);
	c_Player* m_new2();
	int p_Reset();
	Float p_GetCentreX();
	Float p_GetCentreY();
	Float p_GetX();
	Float p_GetY();
	int p_GetWidth();
	int p_GetHeight();
	int p_Hurt();
	int p_GetCurHealth();
	int p_Update();
	int p_UpdateX();
	int p_UseItem1(c_Player*);
	int p_CollRespX();
	int p_UpdateY();
	int p_CollRespY();
	int p_SetX(Float);
	int p_SetY(Float);
	int p_AddItem(c_Interactables*);
	int p_AddWeapon(c_WeaponInterface*);
	int p_Attack(c_List3*);
	int p_Draw2();
	int p_Heal();
	void mark();
	String debug();
};
String dbg_type(c_Player**p){return "Player";}
class c_Animation : public c_GameObject{
	public:
	int m_spriteNum;
	int m_frameTime;
	Float m_xScale;
	Float m_yScale;
	c_Animation();
	c_Animation* m_new(String,int,int,int,int,Float,Float);
	c_Animation* m_new2();
	int p_Play(Float,Float);
	int p_Play2(Float,Float,int);
	void mark();
	String debug();
};
String dbg_type(c_Animation**p){return "Animation";}
class c_Sound : public Object{
	public:
	gxtkSample* m_sample;
	c_Sound();
	c_Sound* m_new(gxtkSample*);
	c_Sound* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Sound**p){return "Sound";}
c_Sound* bb_audio_LoadSound(String);
class c_RoomMap : public Object{
	public:
	int m_MapWidth;
	int m_MapHeight;
	Array<Array<c_Room* > > m_Map;
	c_Room* m_currentRoom;
	int m_roomNum;
	c_Image* m_mapBorder;
	int m_MapPSize;
	int m_MapXOffset;
	c_RoomMap();
	c_RoomMap* m_new();
	int p_Build();
	int p_UpdateCurrentRoom(c_Player*);
	bool p_TileCollision(Float,Float);
	bool p_DamageCollision(Float,Float);
	int p_MoveRoom(String);
	int p_InteractCurrentRoom(c_Player*);
	c_List3* p_GetCurrentRoomEnemies();
	int p_Reset();
	int p_DrawRoomFloor();
	int p_DrawRoomWalls();
	int p_DrawMap();
	void mark();
	String debug();
};
String dbg_type(c_RoomMap**p){return "RoomMap";}
int bb_input_KeyHit(int);
class c_WeaponInterface : public virtual gc_interface{
	public:
	virtual int p_GetX()=0;
	virtual int p_GetY()=0;
	virtual int p_PickUp()=0;
	virtual int p_Attack2(int,int,c_List3*)=0;
	virtual int p_DrawItem()=0;
	virtual int p_Draw(int,int)=0;
};
class c_Interactables : public virtual gc_interface{
	public:
	virtual int p_Use(c_Player*)=0;
	virtual Float p_GetX()=0;
	virtual Float p_GetY()=0;
	virtual int p_PickUp()=0;
	virtual int p_Draw2()=0;
	virtual int p_Draw3(int)=0;
};
int bb_audio_StopMusic();
int bb_app_Millisecs();
extern int bb_random_Seed;
class c_Room : public Object{
	public:
	int m_x;
	int m_y;
	int m_Type;
	c_List* m_droppedItems;
	c_List2* m_droppedWeapons;
	c_List3* m_enemyList;
	Array<Array<int > > m_WallLayout;
	Array<Array<int > > m_FloorLayout;
	Array<Array<int > > m_CollisionArray;
	bool m_visible;
	bool m_current;
	int m_enemyCount;
	int m_Neighbours;
	String m_nDoor;
	String m_sDoor;
	String m_wDoor;
	String m_eDoor;
	bool m_complete;
	int m_RoomSize32;
	c_Image* m_RoomTiles;
	Array<String > m_DoorArray;
	c_Room();
	c_Room* m_new(int,int,int);
	c_Room* m_new2();
	int p_UpdateType(int);
	int p_UpdateVisible(bool);
	int p_UpdateCurrent(bool);
	int p_SetLayout(int);
	int p_UpdateNeighbours(int);
	int p_GetNeighbours();
	int p_GetType();
	int p_GetX();
	int p_GetY();
	int p_SetnDoor(String);
	int p_SetsDoor(String);
	int p_SetwDoor(String);
	int p_SeteDoor(String);
	int p_UpdateRoom(c_Player*);
	bool p_TileCollision(Float,Float);
	bool p_DamageCollision(Float,Float);
	int p_Interact(c_Player*);
	c_List3* p_GetEnemyList();
	int p_Reset();
	int p_DrawFloor();
	int p_DrawEnemies();
	int p_DrawItems();
	int p_DrawWalls();
	bool p_GetVisible();
	bool p_GetCurrent();
	String p_GetDoors();
	void mark();
	String debug();
};
String dbg_type(c_Room**p){return "Room";}
class c_List : public Object{
	public:
	c_Node2* m__head;
	c_List();
	c_List* m_new();
	c_Node2* p_AddLast(c_Interactables*);
	c_List* m_new2(Array<c_Interactables* >);
	c_Enumerator3* p_ObjectEnumerator();
	bool p_IsEmpty();
	c_Interactables* p_RemoveFirst();
	bool p_Equals(c_Interactables*,c_Interactables*);
	c_Node2* p_Find(c_Interactables*,c_Node2*);
	c_Node2* p_Find2(c_Interactables*);
	void p_RemoveFirst2(c_Interactables*);
	void mark();
	String debug();
};
String dbg_type(c_List**p){return "List";}
class c_Node2 : public Object{
	public:
	c_Node2* m__succ;
	c_Node2* m__pred;
	c_Interactables* m__data;
	c_Node2();
	c_Node2* m_new(c_Node2*,c_Node2*,c_Interactables*);
	c_Node2* m_new2();
	int p_Remove();
	void mark();
	String debug();
};
String dbg_type(c_Node2**p){return "Node";}
class c_HeadNode : public c_Node2{
	public:
	c_HeadNode();
	c_HeadNode* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode**p){return "HeadNode";}
class c_List2 : public Object{
	public:
	c_Node3* m__head;
	c_List2();
	c_List2* m_new();
	c_Node3* p_AddLast2(c_WeaponInterface*);
	c_List2* m_new2(Array<c_WeaponInterface* >);
	c_Enumerator4* p_ObjectEnumerator();
	bool p_IsEmpty();
	c_WeaponInterface* p_RemoveFirst();
	bool p_Equals2(c_WeaponInterface*,c_WeaponInterface*);
	c_Node3* p_Find3(c_WeaponInterface*,c_Node3*);
	c_Node3* p_Find4(c_WeaponInterface*);
	void p_RemoveFirst3(c_WeaponInterface*);
	void mark();
	String debug();
};
String dbg_type(c_List2**p){return "List";}
class c_Node3 : public Object{
	public:
	c_Node3* m__succ;
	c_Node3* m__pred;
	c_WeaponInterface* m__data;
	c_Node3();
	c_Node3* m_new(c_Node3*,c_Node3*,c_WeaponInterface*);
	c_Node3* m_new2();
	int p_Remove();
	void mark();
	String debug();
};
String dbg_type(c_Node3**p){return "Node";}
class c_HeadNode2 : public c_Node3{
	public:
	c_HeadNode2();
	c_HeadNode2* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode2**p){return "HeadNode";}
class c_Enemy : public Object{
	public:
	int m_type;
	Float m_maxHealth;
	Float m_curHealth;
	Float m_speed;
	int m_range;
	int m_width;
	int m_height;
	int m_cd;
	Float m_x;
	Float m_y;
	bool m_active;
	c_Animation* m_idle;
	c_Animation* m_right;
	c_Animation* m_left;
	c_Animation* m_summon;
	c_Vec2D* m_vecSpeed;
	Float m_knockSpeed;
	bool m_knockback;
	String m_animState;
	int m_playerDist;
	bool m_charge;
	int m_chargeTime;
	int m_cdTime;
	c_Enemy();
	c_Enemy* m_new(int,Float,Float,bool);
	c_Enemy* m_new2();
	int p_CheckActive(Float,Float);
	int p_CalcMove(Float,Float);
	int p_MoveX();
	int p_GetX();
	int p_GetY();
	int p_GetWidth();
	int p_GetHeight();
	int p_CorrectX();
	int p_MoveY();
	int p_CorrectY();
	int p_GetType();
	int p_Attack3(c_Player*);
	bool p_Summon(c_Player*);
	int p_CheckHealth();
	int p_Draw2();
	int p_Damage(int,int,int,int);
	void mark();
	String debug();
};
String dbg_type(c_Enemy**p){return "Enemy";}
class c_List3 : public Object{
	public:
	c_Node4* m__head;
	c_List3();
	c_List3* m_new();
	c_Node4* p_AddLast3(c_Enemy*);
	c_List3* m_new2(Array<c_Enemy* >);
	int p_Clear();
	c_Enumerator2* p_ObjectEnumerator();
	bool p_Equals3(c_Enemy*,c_Enemy*);
	int p_RemoveEach(c_Enemy*);
	void p_Remove2(c_Enemy*);
	void mark();
	String debug();
};
String dbg_type(c_List3**p){return "List";}
class c_Node4 : public Object{
	public:
	c_Node4* m__succ;
	c_Node4* m__pred;
	c_Enemy* m__data;
	c_Node4();
	c_Node4* m_new(c_Node4*,c_Node4*,c_Enemy*);
	c_Node4* m_new2();
	int p_Remove();
	void mark();
	String debug();
};
String dbg_type(c_Node4**p){return "Node";}
class c_HeadNode3 : public c_Node4{
	public:
	c_HeadNode3();
	c_HeadNode3* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode3**p){return "HeadNode";}
Float bb_random_Rnd();
Float bb_random_Rnd2(Float,Float);
Float bb_random_Rnd3(Float);
class c_Weapon : public c_GameObject{
	public:
	int m_damage;
	String m_state;
	int m_id;
	c_Image* m_guiImage;
	int m_cdTime;
	int m_cd;
	c_Weapon();
	c_Weapon* m_new();
	void mark();
	String debug();
};
String dbg_type(c_Weapon**p){return "Weapon";}
class c_Sword : public c_Weapon,public virtual c_WeaponInterface{
	public:
	c_Sound* m_slashSound;
	c_Animation* m_slashRight;
	c_Animation* m_slashLeft;
	c_Animation* m_slashUp;
	c_Animation* m_slashDown;
	bool m_update;
	int m_angle;
	int m_targetAngle;
	int m_damageWidth;
	int m_damageHeight;
	String m_hitbox;
	int m_angleX;
	int m_angleY;
	c_Sword();
	c_Sword* m_new(int,String,int,int,int);
	c_Sword* m_new2();
	int p_Attack2(int,int,c_List3*);
	int p_DrawItem();
	int p_Draw(int,int);
	int p_GetX();
	int p_GetY();
	int p_PickUp();
	void mark();
	String debug();
};
String dbg_type(c_Sword**p){return "Sword";}
class c_Vec2D : public Object{
	public:
	Float m_VecX;
	Float m_VecY;
	Float m_magnitude;
	c_Vec2D();
	c_Vec2D* m_new(Float,Float);
	c_Vec2D* m_new2();
	int p_Reset();
	int p_UpdateX2(Float);
	int p_UpdateY2(Float);
	int p_Normalise();
	Float p_GetMagnitude();
	Float p_GetX();
	Float p_GetY();
	void mark();
	String debug();
};
String dbg_type(c_Vec2D**p){return "Vec2D";}
class c_List4 : public Object{
	public:
	c_Node5* m__head;
	c_List4();
	c_List4* m_new();
	c_Node5* p_AddLast4(c_Room*);
	c_List4* m_new2(Array<c_Room* >);
	int p_Count();
	c_Enumerator* p_ObjectEnumerator();
	Array<c_Room* > p_ToArray();
	void mark();
	String debug();
};
String dbg_type(c_List4**p){return "List";}
class c_Node5 : public Object{
	public:
	c_Node5* m__succ;
	c_Node5* m__pred;
	c_Room* m__data;
	c_Node5();
	c_Node5* m_new(c_Node5*,c_Node5*,c_Room*);
	c_Node5* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Node5**p){return "Node";}
class c_HeadNode4 : public c_Node5{
	public:
	c_HeadNode4();
	c_HeadNode4* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode4**p){return "HeadNode";}
class c_Enumerator : public Object{
	public:
	c_List4* m__list;
	c_Node5* m__curr;
	c_Enumerator();
	c_Enumerator* m_new(c_List4*);
	c_Enumerator* m_new2();
	bool p_HasNext();
	c_Room* p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_Enumerator**p){return "Enumerator";}
void bb_app_HideMouse();
class c_Enumerator2 : public Object{
	public:
	c_List3* m__list;
	c_Node4* m__curr;
	c_Enumerator2();
	c_Enumerator2* m_new(c_List3*);
	c_Enumerator2* m_new2();
	bool p_HasNext();
	c_Enemy* p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_Enumerator2**p){return "Enumerator";}
bool bb_BasicLibrary_Collide(int,int,int,int,int,int,int,int);
bool bb_Enemies_CheckCollision(c_Enemy*,c_List3*);
int bb_BasicLibrary_findDist(Float,Float,Float,Float);
int bb_audio_PlaySound(c_Sound*,int,int);
class c_Item : public c_GameObject{
	public:
	int m_id;
	String m_state;
	c_Image* m_guiImage;
	c_Item();
	c_Item* m_new(Float,Float,int);
	c_Item* m_new2();
	int p_PickUpItem();
	int p_DrawItem2(int);
	void mark();
	String debug();
};
String dbg_type(c_Item**p){return "Item";}
class c_HealthPotion : public c_Item,public virtual c_Interactables{
	public:
	c_HealthPotion();
	c_HealthPotion* m_new(Float,Float);
	c_HealthPotion* m_new2();
	int p_Use(c_Player*);
	int p_PickUp();
	int p_Draw2();
	int p_Draw3(int);
	Float p_GetX();
	Float p_GetY();
	void mark();
	String debug();
};
String dbg_type(c_HealthPotion**p){return "HealthPotion";}
int bb_input_KeyDown(int);
class c_Enumerator3 : public Object{
	public:
	c_List* m__list;
	c_Node2* m__curr;
	c_Enumerator3();
	c_Enumerator3* m_new(c_List*);
	c_Enumerator3* m_new2();
	bool p_HasNext();
	c_Interactables* p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_Enumerator3**p){return "Enumerator";}
class c_Enumerator4 : public Object{
	public:
	c_List2* m__list;
	c_Node3* m__curr;
	c_Enumerator4();
	c_Enumerator4* m_new(c_List2*);
	c_Enumerator4* m_new2();
	bool p_HasNext();
	c_WeaponInterface* p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_Enumerator4**p){return "Enumerator";}
int bb_graphics_DebugRenderDevice();
int bb_graphics_Cls(Float,Float,Float);
int bb_graphics_DrawImage(c_Image*,Float,Float,int);
int bb_graphics_PushMatrix();
int bb_graphics_Transform(Float,Float,Float,Float,Float,Float);
int bb_graphics_Transform2(Array<Float >);
int bb_graphics_Translate(Float,Float);
int bb_graphics_Rotate(Float);
int bb_graphics_Scale(Float,Float);
int bb_graphics_PopMatrix();
int bb_graphics_DrawImage2(c_Image*,Float,Float,Float,Float,Float,int);
int bb_graphics_DrawRect(Float,Float,Float,Float);
int bb_LevelGeneration_DrawRoom(String,int,int,int,int);
int bb_graphics_DrawText(String,Float,Float,Float,Float);
Float bb_input_MouseX();
Float bb_input_MouseY();
int bb_input_MouseHit(int);
c_App::c_App(){
}
c_App* c_App::m_new(){
	DBG_ENTER("App.new")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<152>");
	if((bb_app__app)!=0){
		DBG_BLOCK();
		bbError(String(L"App has already been created",28));
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<153>");
	gc_assign(bb_app__app,this);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<154>");
	gc_assign(bb_app__delegate,(new c_GameDelegate)->m_new());
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<155>");
	bb_app__game->SetDelegate(bb_app__delegate);
	return this;
}
int c_App::p_OnResize(){
	DBG_ENTER("App.OnResize")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnCreate(){
	DBG_ENTER("App.OnCreate")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnSuspend(){
	DBG_ENTER("App.OnSuspend")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnResume(){
	DBG_ENTER("App.OnResume")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnUpdate(){
	DBG_ENTER("App.OnUpdate")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnLoading(){
	DBG_ENTER("App.OnLoading")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnRender(){
	DBG_ENTER("App.OnRender")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnClose(){
	DBG_ENTER("App.OnClose")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<177>");
	bb_app_EndApp();
	return 0;
}
int c_App::p_OnBack(){
	DBG_ENTER("App.OnBack")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<181>");
	p_OnClose();
	return 0;
}
void c_App::mark(){
	Object::mark();
}
String c_App::debug(){
	String t="(App)\n";
	return t;
}
c_Game::c_Game(){
	m_menuBackground=0;
	m_menuBut=0;
	m_optionsBut=0;
	m_exitBut=0;
	m_playBut=0;
	m_menuArray=Array<c_Button* >();
	m_optionsArray=Array<c_Button* >();
	m_deadArray=Array<c_Button* >();
	m_effectsVol=0;
	m_musicVol=0;
	m_optionsSliders=Array<c_VolumeSlider* >();
	m_xhair=0;
	m_p1=0;
	m_Room1=0;
	m_Room2=0;
	m_Room3=0;
	m_gameState=String(L"MENU",4);
	m_butIndex=0;
	m_optIndex=0;
}
c_Game* c_Game::m_new(){
	DBG_ENTER("Game.new")
	c_Game *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/Ragnarok.monkey<15>");
	c_App::m_new();
	return this;
}
int c_Game::p_OnCreate(){
	DBG_ENTER("Game.OnCreate")
	c_Game *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/Ragnarok.monkey<42>");
	bb_app_SetDeviceWindow(640,480,1);
	DBG_INFO("F:/Final Game/Ragnarok.monkey<43>");
	bb_app_SetUpdateRate(60);
	DBG_INFO("F:/Final Game/Ragnarok.monkey<44>");
	bb_audio_SetMusicVolume(FLOAT(0.5));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<45>");
	gc_assign(m_menuBackground,bb_graphics_LoadImage(String(L"menus/mainMenu.png",18),1,c_Image::m_DefaultFlags));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<46>");
	gc_assign(m_menuBut,(new c_Button)->m_new(String(L"menus/menuButton.png",20)));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<47>");
	m_menuBut->p_SetAction(String(L"MENU",4));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<48>");
	gc_assign(m_optionsBut,(new c_Button)->m_new(String(L"menus/optionsButton.png",23)));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<49>");
	m_optionsBut->p_SetAction(String(L"OPTIONS",7));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<50>");
	gc_assign(m_exitBut,(new c_Button)->m_new(String(L"menus/exitButton.png",20)));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<51>");
	m_exitBut->p_SetAction(String(L"EXIT",4));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<52>");
	gc_assign(m_playBut,(new c_Button)->m_new(String(L"menus/playButton.png",20)));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<53>");
	m_playBut->p_SetAction(String(L"PLAY",4));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<54>");
	c_Button* t_[]={m_playBut,m_optionsBut,m_exitBut};
	gc_assign(m_menuArray,Array<c_Button* >(t_,3));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<55>");
	c_Button* t_2[]={m_menuBut};
	gc_assign(m_optionsArray,Array<c_Button* >(t_2,1));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<56>");
	c_Button* t_3[]={m_menuBut};
	gc_assign(m_deadArray,Array<c_Button* >(t_3,1));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<57>");
	gc_assign(m_effectsVol,(new c_VolumeSlider)->m_new(10,300,225,10,30));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<58>");
	gc_assign(m_musicVol,(new c_VolumeSlider)->m_new(10,300,300,10,30));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<59>");
	c_VolumeSlider* t_4[]={m_effectsVol,m_musicVol};
	gc_assign(m_optionsSliders,Array<c_VolumeSlider* >(t_4,2));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<60>");
	bb_audio_PlayMusic(String(L"menus/menuMusic.wav",19),1);
	DBG_INFO("F:/Final Game/Ragnarok.monkey<62>");
	gc_assign(m_xhair,(new c_Crosshair)->m_new(0));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<63>");
	gc_assign(m_p1,(new c_Player)->m_new(FLOAT(200.0),FLOAT(200.0),3,16,24));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<65>");
	gc_assign(m_Room1,(new c_RoomMap)->m_new());
	DBG_INFO("F:/Final Game/Ragnarok.monkey<66>");
	gc_assign(m_Room2,(new c_RoomMap)->m_new());
	DBG_INFO("F:/Final Game/Ragnarok.monkey<67>");
	gc_assign(m_Room3,(new c_RoomMap)->m_new());
	return 0;
}
int c_Game::p_OnUpdate(){
	DBG_ENTER("Game.OnUpdate")
	c_Game *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/Ragnarok.monkey<71>");
	String t_1=m_gameState;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("F:/Final Game/Ragnarok.monkey<72>");
	if(t_1==String(L"MENU",4)){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/Ragnarok.monkey<73>");
		m_menuArray.At(m_butIndex)->p_SetActive(true);
		DBG_INFO("F:/Final Game/Ragnarok.monkey<74>");
		if(((bb_input_KeyHit(83))!=0) && m_butIndex<2){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/Ragnarok.monkey<75>");
			m_menuArray.At(m_butIndex)->p_SetActive(false);
			DBG_INFO("F:/Final Game/Ragnarok.monkey<76>");
			m_butIndex+=1;
			DBG_INFO("F:/Final Game/Ragnarok.monkey<77>");
			m_menuArray.At(m_butIndex)->p_SetActive(true);
		}
		DBG_INFO("F:/Final Game/Ragnarok.monkey<79>");
		if(((bb_input_KeyHit(87))!=0) && m_butIndex>0){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/Ragnarok.monkey<80>");
			m_menuArray.At(m_butIndex)->p_SetActive(false);
			DBG_INFO("F:/Final Game/Ragnarok.monkey<81>");
			m_butIndex-=1;
			DBG_INFO("F:/Final Game/Ragnarok.monkey<82>");
			m_menuArray.At(m_butIndex)->p_SetActive(true);
		}
		DBG_INFO("F:/Final Game/Ragnarok.monkey<84>");
		if((bb_input_KeyHit(32))!=0){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/Ragnarok.monkey<85>");
			if(m_menuArray.At(m_butIndex)->p_GetAction()==String(L"PLAY",4)){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<86>");
				m_p1->p_Reset();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<87>");
				bb_audio_StopMusic();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<88>");
				bb_audio_PlayMusic(String(L"music/level1.mp3",16),1);
				DBG_INFO("F:/Final Game/Ragnarok.monkey<89>");
				bb_random_Seed=bb_app_Millisecs();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<90>");
				m_Room1->p_Build();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<91>");
				m_Room2->p_Build();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<92>");
				m_Room3->p_Build();
			}
			DBG_INFO("F:/Final Game/Ragnarok.monkey<94>");
			m_gameState=m_menuArray.At(m_butIndex)->p_GetAction();
			DBG_INFO("F:/Final Game/Ragnarok.monkey<95>");
			m_menuArray.At(m_butIndex)->p_SetActive(false);
			DBG_INFO("F:/Final Game/Ragnarok.monkey<96>");
			m_butIndex=0;
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/Ragnarok.monkey<98>");
		if(t_1==String(L"OPTIONS",7)){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/Ragnarok.monkey<99>");
			int t_2=m_optIndex;
			DBG_LOCAL(t_2,"2")
			DBG_INFO("F:/Final Game/Ragnarok.monkey<100>");
			if(t_2==0){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<101>");
				if((bb_input_KeyHit(83))!=0){
					DBG_BLOCK();
					m_optIndex+=1;
				}
				DBG_INFO("F:/Final Game/Ragnarok.monkey<102>");
				m_effectsVol->p_updateVol();
			}else{
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<103>");
				if(t_2==1){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/Ragnarok.monkey<104>");
					if((bb_input_KeyHit(87))!=0){
						DBG_BLOCK();
						m_optIndex-=1;
					}
					DBG_INFO("F:/Final Game/Ragnarok.monkey<105>");
					if((bb_input_KeyHit(83))!=0){
						DBG_BLOCK();
						m_optIndex+=1;
					}
					DBG_INFO("F:/Final Game/Ragnarok.monkey<106>");
					m_musicVol->p_updateVol();
				}else{
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/Ragnarok.monkey<107>");
					if(t_2==2){
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/Ragnarok.monkey<108>");
						m_optionsArray.At(m_butIndex)->p_SetActive(true);
						DBG_INFO("F:/Final Game/Ragnarok.monkey<109>");
						if((bb_input_KeyHit(32))!=0){
							DBG_BLOCK();
							DBG_INFO("F:/Final Game/Ragnarok.monkey<110>");
							m_optionsArray.At(m_butIndex)->p_SetActive(false);
							DBG_INFO("F:/Final Game/Ragnarok.monkey<111>");
							m_gameState=m_optionsArray.At(m_butIndex)->p_GetAction();
						}
						DBG_INFO("F:/Final Game/Ragnarok.monkey<113>");
						if((bb_input_KeyHit(87))!=0){
							DBG_BLOCK();
							DBG_INFO("F:/Final Game/Ragnarok.monkey<114>");
							m_optIndex-=1;
							DBG_INFO("F:/Final Game/Ragnarok.monkey<115>");
							m_optionsArray.At(m_butIndex)->p_SetActive(false);
						}
					}
				}
			}
			DBG_INFO("F:/Final Game/Ragnarok.monkey<118>");
			bb_audio_SetMusicVolume(m_musicVol->p_getVol());
		}else{
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/Ragnarok.monkey<119>");
			if(t_1==String(L"PLAY",4)){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<120>");
				bb_app_HideMouse();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<121>");
				m_Room1->p_UpdateCurrentRoom(m_p1);
				DBG_INFO("F:/Final Game/Ragnarok.monkey<122>");
				m_p1->p_Update();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<123>");
				m_p1->p_UpdateX();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<124>");
				m_p1->p_UseItem1(m_p1);
				DBG_INFO("F:/Final Game/Ragnarok.monkey<125>");
				if(m_Room1->p_TileCollision(m_p1->p_GetX(),(Float)floor(m_p1->p_GetY()))){
					DBG_BLOCK();
					m_p1->p_CollRespX();
				}
				DBG_INFO("F:/Final Game/Ragnarok.monkey<126>");
				m_p1->p_UpdateY();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<127>");
				if(m_Room1->p_TileCollision(m_p1->p_GetX(),(Float)floor(m_p1->p_GetY()))){
					DBG_BLOCK();
					m_p1->p_CollRespY();
				}
				DBG_INFO("F:/Final Game/Ragnarok.monkey<129>");
				if(m_Room1->p_DamageCollision(m_p1->p_GetX(),(Float)floor(m_p1->p_GetY()))){
					DBG_BLOCK();
					m_p1->p_Hurt();
				}
				DBG_INFO("F:/Final Game/Ragnarok.monkey<131>");
				if(m_p1->p_GetX()<=FLOAT(0.0)){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/Ragnarok.monkey<132>");
					m_Room1->p_MoveRoom(String(L"EAST",4));
					DBG_INFO("F:/Final Game/Ragnarok.monkey<133>");
					m_p1->p_SetX(FLOAT(400.0));
				}
				DBG_INFO("F:/Final Game/Ragnarok.monkey<135>");
				if(m_p1->p_GetX()>=FLOAT(440.0)){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/Ragnarok.monkey<136>");
					m_Room1->p_MoveRoom(String(L"WEST",4));
					DBG_INFO("F:/Final Game/Ragnarok.monkey<137>");
					m_p1->p_SetX(FLOAT(50.0));
				}
				DBG_INFO("F:/Final Game/Ragnarok.monkey<139>");
				if(m_p1->p_GetY()<=FLOAT(0.0)){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/Ragnarok.monkey<140>");
					m_Room1->p_MoveRoom(String(L"NORTH",5));
					DBG_INFO("F:/Final Game/Ragnarok.monkey<141>");
					m_p1->p_SetY(FLOAT(400.0));
				}
				DBG_INFO("F:/Final Game/Ragnarok.monkey<143>");
				if(m_p1->p_GetY()>=FLOAT(440.0)){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/Ragnarok.monkey<144>");
					m_Room1->p_MoveRoom(String(L"SOUTH",5));
					DBG_INFO("F:/Final Game/Ragnarok.monkey<145>");
					m_p1->p_SetY(FLOAT(50.0));
				}
				DBG_INFO("F:/Final Game/Ragnarok.monkey<147>");
				if((bb_input_KeyHit(70))!=0){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/Ragnarok.monkey<148>");
					m_Room1->p_InteractCurrentRoom(m_p1);
				}
				DBG_INFO("F:/Final Game/Ragnarok.monkey<150>");
				m_p1->p_Attack(m_Room1->p_GetCurrentRoomEnemies());
				DBG_INFO("F:/Final Game/Ragnarok.monkey<151>");
				if((bb_input_KeyHit(27))!=0){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/Ragnarok.monkey<152>");
					m_Room1->p_Reset();
					DBG_INFO("F:/Final Game/Ragnarok.monkey<153>");
					m_Room2->p_Reset();
					DBG_INFO("F:/Final Game/Ragnarok.monkey<154>");
					m_Room3->p_Reset();
					DBG_INFO("F:/Final Game/Ragnarok.monkey<155>");
					m_butIndex=0;
					DBG_INFO("F:/Final Game/Ragnarok.monkey<156>");
					m_gameState=String(L"MENU",4);
				}
				DBG_INFO("F:/Final Game/Ragnarok.monkey<159>");
				if(m_p1->p_GetCurHealth()==0){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/Ragnarok.monkey<160>");
					m_butIndex=0;
					DBG_INFO("F:/Final Game/Ragnarok.monkey<161>");
					bb_audio_StopMusic();
					DBG_INFO("F:/Final Game/Ragnarok.monkey<162>");
					m_gameState=String(L"DEAD",4);
				}
			}else{
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<164>");
				if(t_1==String(L"DEAD",4)){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/Ragnarok.monkey<165>");
					m_deadArray.At(m_butIndex)->p_SetActive(true);
					DBG_INFO("F:/Final Game/Ragnarok.monkey<166>");
					if((bb_input_KeyHit(32))!=0){
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/Ragnarok.monkey<167>");
						m_deadArray.At(m_butIndex)->p_SetActive(false);
						DBG_INFO("F:/Final Game/Ragnarok.monkey<168>");
						m_gameState=m_deadArray.At(m_butIndex)->p_GetAction();
						DBG_INFO("F:/Final Game/Ragnarok.monkey<169>");
						bb_audio_PlayMusic(String(L"menus/menuMusic.wav",19),1);
					}
				}else{
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/Ragnarok.monkey<172>");
					if(t_1==String(L"EXIT",4)){
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/Ragnarok.monkey<173>");
						bb_app_EndApp();
					}
				}
			}
		}
	}
	return 0;
}
int c_Game::p_OnRender(){
	DBG_ENTER("Game.OnRender")
	c_Game *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/Ragnarok.monkey<178>");
	bb_graphics_Cls(FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("F:/Final Game/Ragnarok.monkey<179>");
	String t_3=m_gameState;
	DBG_LOCAL(t_3,"3")
	DBG_INFO("F:/Final Game/Ragnarok.monkey<180>");
	if(t_3==String(L"MENU",4)){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/Ragnarok.monkey<181>");
		bb_graphics_DrawImage(m_menuBackground,FLOAT(0.0),FLOAT(0.0),0);
		DBG_INFO("F:/Final Game/Ragnarok.monkey<182>");
		m_playBut->p_Draw(160,225);
		DBG_INFO("F:/Final Game/Ragnarok.monkey<183>");
		m_optionsBut->p_Draw(160,300);
		DBG_INFO("F:/Final Game/Ragnarok.monkey<184>");
		m_exitBut->p_Draw(160,375);
	}else{
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/Ragnarok.monkey<185>");
		if(t_3==String(L"OPTIONS",7)){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/Ragnarok.monkey<186>");
			bb_graphics_DrawImage(m_menuBackground,FLOAT(0.0),FLOAT(0.0),0);
			DBG_INFO("F:/Final Game/Ragnarok.monkey<187>");
			m_effectsVol->p_Draw2();
			DBG_INFO("F:/Final Game/Ragnarok.monkey<188>");
			m_musicVol->p_Draw2();
			DBG_INFO("F:/Final Game/Ragnarok.monkey<189>");
			m_menuBut->p_Draw(160,375);
		}else{
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/Ragnarok.monkey<190>");
			if(t_3==String(L"PLAY",4)){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<192>");
				m_Room1->p_DrawRoomFloor();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<193>");
				m_p1->p_Draw2();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<195>");
				m_Room1->p_DrawRoomWalls();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<196>");
				m_Room1->p_DrawMap();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<198>");
				bb_graphics_DrawText(String(L"Press ESC to reset",18),FLOAT(500.0),FLOAT(255.0),FLOAT(0.0),FLOAT(0.0));
				DBG_INFO("F:/Final Game/Ragnarok.monkey<199>");
				bb_graphics_DrawText(String(L"Seed:",5),FLOAT(500.0),FLOAT(300.0),FLOAT(0.0),FLOAT(0.0));
				DBG_INFO("F:/Final Game/Ragnarok.monkey<200>");
				bb_graphics_DrawText(String(bb_random_Seed),FLOAT(545.0),FLOAT(300.0),FLOAT(0.0),FLOAT(0.0));
				DBG_INFO("F:/Final Game/Ragnarok.monkey<201>");
				m_xhair->p_Draw2();
			}else{
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/Ragnarok.monkey<202>");
				if(t_3==String(L"DEAD",4)){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/Ragnarok.monkey<203>");
					m_menuBut->p_Draw(160,375);
				}
			}
		}
	}
	return 0;
}
void c_Game::mark(){
	c_App::mark();
	gc_mark_q(m_menuBackground);
	gc_mark_q(m_menuBut);
	gc_mark_q(m_optionsBut);
	gc_mark_q(m_exitBut);
	gc_mark_q(m_playBut);
	gc_mark_q(m_menuArray);
	gc_mark_q(m_optionsArray);
	gc_mark_q(m_deadArray);
	gc_mark_q(m_effectsVol);
	gc_mark_q(m_musicVol);
	gc_mark_q(m_optionsSliders);
	gc_mark_q(m_xhair);
	gc_mark_q(m_p1);
	gc_mark_q(m_Room1);
	gc_mark_q(m_Room2);
	gc_mark_q(m_Room3);
}
String c_Game::debug(){
	String t="(Game)\n";
	t=c_App::debug()+t;
	t+=dbg_decl("gameState",&m_gameState);
	t+=dbg_decl("menuBackground",&m_menuBackground);
	t+=dbg_decl("menuBut",&m_menuBut);
	t+=dbg_decl("optionsBut",&m_optionsBut);
	t+=dbg_decl("exitBut",&m_exitBut);
	t+=dbg_decl("playBut",&m_playBut);
	t+=dbg_decl("menuArray",&m_menuArray);
	t+=dbg_decl("optionsArray",&m_optionsArray);
	t+=dbg_decl("optionsSliders",&m_optionsSliders);
	t+=dbg_decl("deadArray",&m_deadArray);
	t+=dbg_decl("effectsVol",&m_effectsVol);
	t+=dbg_decl("musicVol",&m_musicVol);
	t+=dbg_decl("butIndex",&m_butIndex);
	t+=dbg_decl("optIndex",&m_optIndex);
	t+=dbg_decl("xhair",&m_xhair);
	t+=dbg_decl("p1",&m_p1);
	t+=dbg_decl("Room1",&m_Room1);
	t+=dbg_decl("Room2",&m_Room2);
	t+=dbg_decl("Room3",&m_Room3);
	return t;
}
c_App* bb_app__app;
c_GameDelegate::c_GameDelegate(){
	m__graphics=0;
	m__audio=0;
	m__input=0;
}
c_GameDelegate* c_GameDelegate::m_new(){
	DBG_ENTER("GameDelegate.new")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<65>");
	return this;
}
void c_GameDelegate::StartGame(){
	DBG_ENTER("GameDelegate.StartGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<75>");
	gc_assign(m__graphics,(new gxtkGraphics));
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<76>");
	bb_graphics_SetGraphicsDevice(m__graphics);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<77>");
	bb_graphics_SetFont(0,32);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<79>");
	gc_assign(m__audio,(new gxtkAudio));
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<80>");
	bb_audio_SetAudioDevice(m__audio);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<82>");
	gc_assign(m__input,(new c_InputDevice)->m_new());
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<83>");
	bb_input_SetInputDevice(m__input);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<85>");
	bb_app_ValidateDeviceWindow(false);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<87>");
	bb_app_EnumDisplayModes();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<89>");
	bb_app__app->p_OnCreate();
}
void c_GameDelegate::SuspendGame(){
	DBG_ENTER("GameDelegate.SuspendGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<93>");
	bb_app__app->p_OnSuspend();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<94>");
	m__audio->Suspend();
}
void c_GameDelegate::ResumeGame(){
	DBG_ENTER("GameDelegate.ResumeGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<98>");
	m__audio->Resume();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<99>");
	bb_app__app->p_OnResume();
}
void c_GameDelegate::UpdateGame(){
	DBG_ENTER("GameDelegate.UpdateGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<103>");
	bb_app_ValidateDeviceWindow(true);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<104>");
	m__input->p_BeginUpdate();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<105>");
	bb_app__app->p_OnUpdate();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<106>");
	m__input->p_EndUpdate();
}
void c_GameDelegate::RenderGame(){
	DBG_ENTER("GameDelegate.RenderGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<110>");
	bb_app_ValidateDeviceWindow(true);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<111>");
	int t_mode=m__graphics->BeginRender();
	DBG_LOCAL(t_mode,"mode")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<112>");
	if((t_mode)!=0){
		DBG_BLOCK();
		bb_graphics_BeginRender();
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<113>");
	if(t_mode==2){
		DBG_BLOCK();
		bb_app__app->p_OnLoading();
	}else{
		DBG_BLOCK();
		bb_app__app->p_OnRender();
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<114>");
	if((t_mode)!=0){
		DBG_BLOCK();
		bb_graphics_EndRender();
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<115>");
	m__graphics->EndRender();
}
void c_GameDelegate::KeyEvent(int t_event,int t_data){
	DBG_ENTER("GameDelegate.KeyEvent")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<119>");
	m__input->p_KeyEvent(t_event,t_data);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<120>");
	if(t_event!=1){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<121>");
	int t_1=t_data;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<122>");
	if(t_1==432){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<123>");
		bb_app__app->p_OnClose();
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<124>");
		if(t_1==416){
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<125>");
			bb_app__app->p_OnBack();
		}
	}
}
void c_GameDelegate::MouseEvent(int t_event,int t_data,Float t_x,Float t_y){
	DBG_ENTER("GameDelegate.MouseEvent")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<130>");
	m__input->p_MouseEvent(t_event,t_data,t_x,t_y);
}
void c_GameDelegate::TouchEvent(int t_event,int t_data,Float t_x,Float t_y){
	DBG_ENTER("GameDelegate.TouchEvent")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<134>");
	m__input->p_TouchEvent(t_event,t_data,t_x,t_y);
}
void c_GameDelegate::MotionEvent(int t_event,int t_data,Float t_x,Float t_y,Float t_z){
	DBG_ENTER("GameDelegate.MotionEvent")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_z,"z")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<138>");
	m__input->p_MotionEvent(t_event,t_data,t_x,t_y,t_z);
}
void c_GameDelegate::DiscardGraphics(){
	DBG_ENTER("GameDelegate.DiscardGraphics")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<142>");
	m__graphics->DiscardGraphics();
}
void c_GameDelegate::mark(){
	BBGameDelegate::mark();
	gc_mark_q(m__graphics);
	gc_mark_q(m__audio);
	gc_mark_q(m__input);
}
String c_GameDelegate::debug(){
	String t="(GameDelegate)\n";
	t+=dbg_decl("_graphics",&m__graphics);
	t+=dbg_decl("_audio",&m__audio);
	t+=dbg_decl("_input",&m__input);
	return t;
}
c_GameDelegate* bb_app__delegate;
BBGame* bb_app__game;
c_Game* bb_Ragnarok_game;
int bbMain(){
	DBG_ENTER("Main")
	DBG_INFO("F:/Final Game/Ragnarok.monkey<12>");
	gc_assign(bb_Ragnarok_game,(new c_Game)->m_new());
	return 0;
}
gxtkGraphics* bb_graphics_device;
int bb_graphics_SetGraphicsDevice(gxtkGraphics* t_dev){
	DBG_ENTER("SetGraphicsDevice")
	DBG_LOCAL(t_dev,"dev")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<63>");
	gc_assign(bb_graphics_device,t_dev);
	return 0;
}
c_Image::c_Image(){
	m_surface=0;
	m_width=0;
	m_height=0;
	m_frames=Array<c_Frame* >();
	m_flags=0;
	m_tx=FLOAT(.0);
	m_ty=FLOAT(.0);
	m_source=0;
}
int c_Image::m_DefaultFlags;
c_Image* c_Image::m_new(){
	DBG_ENTER("Image.new")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<70>");
	return this;
}
int c_Image::p_SetHandle(Float t_tx,Float t_ty){
	DBG_ENTER("Image.SetHandle")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_tx,"tx")
	DBG_LOCAL(t_ty,"ty")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<114>");
	this->m_tx=t_tx;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<115>");
	this->m_ty=t_ty;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<116>");
	this->m_flags=this->m_flags&-2;
	return 0;
}
int c_Image::p_ApplyFlags(int t_iflags){
	DBG_ENTER("Image.ApplyFlags")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_iflags,"iflags")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<192>");
	m_flags=t_iflags;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<194>");
	if((m_flags&2)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<195>");
		Array<c_Frame* > t_=m_frames;
		int t_2=0;
		while(t_2<t_.Length()){
			DBG_BLOCK();
			c_Frame* t_f=t_.At(t_2);
			t_2=t_2+1;
			DBG_LOCAL(t_f,"f")
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<196>");
			t_f->m_x+=1;
		}
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<198>");
		m_width-=2;
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<201>");
	if((m_flags&4)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<202>");
		Array<c_Frame* > t_3=m_frames;
		int t_4=0;
		while(t_4<t_3.Length()){
			DBG_BLOCK();
			c_Frame* t_f2=t_3.At(t_4);
			t_4=t_4+1;
			DBG_LOCAL(t_f2,"f")
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<203>");
			t_f2->m_y+=1;
		}
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<205>");
		m_height-=2;
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<208>");
	if((m_flags&1)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<209>");
		p_SetHandle(Float(m_width)/FLOAT(2.0),Float(m_height)/FLOAT(2.0));
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<212>");
	if(m_frames.Length()==1 && m_frames.At(0)->m_x==0 && m_frames.At(0)->m_y==0 && m_width==m_surface->Width() && m_height==m_surface->Height()){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<213>");
		m_flags|=65536;
	}
	return 0;
}
c_Image* c_Image::p_Init(gxtkSurface* t_surf,int t_nframes,int t_iflags){
	DBG_ENTER("Image.Init")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_surf,"surf")
	DBG_LOCAL(t_nframes,"nframes")
	DBG_LOCAL(t_iflags,"iflags")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<150>");
	gc_assign(m_surface,t_surf);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<152>");
	m_width=m_surface->Width()/t_nframes;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<153>");
	m_height=m_surface->Height();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<155>");
	gc_assign(m_frames,Array<c_Frame* >(t_nframes));
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<156>");
	for(int t_i=0;t_i<t_nframes;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<157>");
		gc_assign(m_frames.At(t_i),(new c_Frame)->m_new(t_i*m_width,0));
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<160>");
	p_ApplyFlags(t_iflags);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<161>");
	return this;
}
c_Image* c_Image::p_Init2(gxtkSurface* t_surf,int t_x,int t_y,int t_iwidth,int t_iheight,int t_nframes,int t_iflags,c_Image* t_src,int t_srcx,int t_srcy,int t_srcw,int t_srch){
	DBG_ENTER("Image.Init")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_surf,"surf")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_iwidth,"iwidth")
	DBG_LOCAL(t_iheight,"iheight")
	DBG_LOCAL(t_nframes,"nframes")
	DBG_LOCAL(t_iflags,"iflags")
	DBG_LOCAL(t_src,"src")
	DBG_LOCAL(t_srcx,"srcx")
	DBG_LOCAL(t_srcy,"srcy")
	DBG_LOCAL(t_srcw,"srcw")
	DBG_LOCAL(t_srch,"srch")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<165>");
	gc_assign(m_surface,t_surf);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<166>");
	gc_assign(m_source,t_src);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<168>");
	m_width=t_iwidth;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<169>");
	m_height=t_iheight;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<171>");
	gc_assign(m_frames,Array<c_Frame* >(t_nframes));
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<173>");
	int t_ix=t_x;
	int t_iy=t_y;
	DBG_LOCAL(t_ix,"ix")
	DBG_LOCAL(t_iy,"iy")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<175>");
	for(int t_i=0;t_i<t_nframes;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<176>");
		if(t_ix+m_width>t_srcw){
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<177>");
			t_ix=0;
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<178>");
			t_iy+=m_height;
		}
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<180>");
		if(t_ix+m_width>t_srcw || t_iy+m_height>t_srch){
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<181>");
			bbError(String(L"Image frame outside surface",27));
		}
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<183>");
		gc_assign(m_frames.At(t_i),(new c_Frame)->m_new(t_ix+t_srcx,t_iy+t_srcy));
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<184>");
		t_ix+=m_width;
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<187>");
	p_ApplyFlags(t_iflags);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<188>");
	return this;
}
c_Image* c_Image::p_GrabImage(int t_x,int t_y,int t_width,int t_height,int t_nframes,int t_flags){
	DBG_ENTER("Image.GrabImage")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_width,"width")
	DBG_LOCAL(t_height,"height")
	DBG_LOCAL(t_nframes,"nframes")
	DBG_LOCAL(t_flags,"flags")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<109>");
	if(m_frames.Length()!=1){
		DBG_BLOCK();
		return 0;
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<110>");
	c_Image* t_=((new c_Image)->m_new())->p_Init2(m_surface,t_x,t_y,t_width,t_height,t_nframes,t_flags,this,m_frames.At(0)->m_x,m_frames.At(0)->m_y,this->m_width,this->m_height);
	return t_;
}
int c_Image::p_Width(){
	DBG_ENTER("Image.Width")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<81>");
	return m_width;
}
int c_Image::p_Height(){
	DBG_ENTER("Image.Height")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<85>");
	return m_height;
}
int c_Image::p_Frames(){
	DBG_ENTER("Image.Frames")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<93>");
	int t_=m_frames.Length();
	return t_;
}
void c_Image::mark(){
	Object::mark();
	gc_mark_q(m_surface);
	gc_mark_q(m_frames);
	gc_mark_q(m_source);
}
String c_Image::debug(){
	String t="(Image)\n";
	t+=dbg_decl("DefaultFlags",&c_Image::m_DefaultFlags);
	t+=dbg_decl("source",&m_source);
	t+=dbg_decl("surface",&m_surface);
	t+=dbg_decl("width",&m_width);
	t+=dbg_decl("height",&m_height);
	t+=dbg_decl("flags",&m_flags);
	t+=dbg_decl("frames",&m_frames);
	t+=dbg_decl("tx",&m_tx);
	t+=dbg_decl("ty",&m_ty);
	return t;
}
c_GraphicsContext::c_GraphicsContext(){
	m_defaultFont=0;
	m_font=0;
	m_firstChar=0;
	m_matrixSp=0;
	m_ix=FLOAT(1.0);
	m_iy=FLOAT(.0);
	m_jx=FLOAT(.0);
	m_jy=FLOAT(1.0);
	m_tx=FLOAT(.0);
	m_ty=FLOAT(.0);
	m_tformed=0;
	m_matDirty=0;
	m_color_r=FLOAT(.0);
	m_color_g=FLOAT(.0);
	m_color_b=FLOAT(.0);
	m_alpha=FLOAT(.0);
	m_blend=0;
	m_scissor_x=FLOAT(.0);
	m_scissor_y=FLOAT(.0);
	m_scissor_width=FLOAT(.0);
	m_scissor_height=FLOAT(.0);
	m_matrixStack=Array<Float >(192);
}
c_GraphicsContext* c_GraphicsContext::m_new(){
	DBG_ENTER("GraphicsContext.new")
	c_GraphicsContext *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<29>");
	return this;
}
int c_GraphicsContext::p_Validate(){
	DBG_ENTER("GraphicsContext.Validate")
	c_GraphicsContext *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<40>");
	if((m_matDirty)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<41>");
		bb_graphics_renderDevice->SetMatrix(bb_graphics_context->m_ix,bb_graphics_context->m_iy,bb_graphics_context->m_jx,bb_graphics_context->m_jy,bb_graphics_context->m_tx,bb_graphics_context->m_ty);
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<42>");
		m_matDirty=0;
	}
	return 0;
}
void c_GraphicsContext::mark(){
	Object::mark();
	gc_mark_q(m_defaultFont);
	gc_mark_q(m_font);
	gc_mark_q(m_matrixStack);
}
String c_GraphicsContext::debug(){
	String t="(GraphicsContext)\n";
	t+=dbg_decl("color_r",&m_color_r);
	t+=dbg_decl("color_g",&m_color_g);
	t+=dbg_decl("color_b",&m_color_b);
	t+=dbg_decl("alpha",&m_alpha);
	t+=dbg_decl("blend",&m_blend);
	t+=dbg_decl("ix",&m_ix);
	t+=dbg_decl("iy",&m_iy);
	t+=dbg_decl("jx",&m_jx);
	t+=dbg_decl("jy",&m_jy);
	t+=dbg_decl("tx",&m_tx);
	t+=dbg_decl("ty",&m_ty);
	t+=dbg_decl("tformed",&m_tformed);
	t+=dbg_decl("matDirty",&m_matDirty);
	t+=dbg_decl("scissor_x",&m_scissor_x);
	t+=dbg_decl("scissor_y",&m_scissor_y);
	t+=dbg_decl("scissor_width",&m_scissor_width);
	t+=dbg_decl("scissor_height",&m_scissor_height);
	t+=dbg_decl("matrixStack",&m_matrixStack);
	t+=dbg_decl("matrixSp",&m_matrixSp);
	t+=dbg_decl("font",&m_font);
	t+=dbg_decl("firstChar",&m_firstChar);
	t+=dbg_decl("defaultFont",&m_defaultFont);
	return t;
}
c_GraphicsContext* bb_graphics_context;
String bb_data_FixDataPath(String t_path){
	DBG_ENTER("FixDataPath")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/data.monkey<7>");
	int t_i=t_path.Find(String(L":/",2),0);
	DBG_LOCAL(t_i,"i")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/data.monkey<8>");
	if(t_i!=-1 && t_path.Find(String(L"/",1),0)==t_i+1){
		DBG_BLOCK();
		return t_path;
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/data.monkey<9>");
	if(t_path.StartsWith(String(L"./",2)) || t_path.StartsWith(String(L"/",1))){
		DBG_BLOCK();
		return t_path;
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/data.monkey<10>");
	String t_=String(L"monkey://data/",14)+t_path;
	return t_;
}
c_Frame::c_Frame(){
	m_x=0;
	m_y=0;
}
c_Frame* c_Frame::m_new(int t_x,int t_y){
	DBG_ENTER("Frame.new")
	c_Frame *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<23>");
	this->m_x=t_x;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<24>");
	this->m_y=t_y;
	return this;
}
c_Frame* c_Frame::m_new2(){
	DBG_ENTER("Frame.new")
	c_Frame *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<18>");
	return this;
}
void c_Frame::mark(){
	Object::mark();
}
String c_Frame::debug(){
	String t="(Frame)\n";
	t+=dbg_decl("x",&m_x);
	t+=dbg_decl("y",&m_y);
	return t;
}
c_Image* bb_graphics_LoadImage(String t_path,int t_frameCount,int t_flags){
	DBG_ENTER("LoadImage")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_frameCount,"frameCount")
	DBG_LOCAL(t_flags,"flags")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<244>");
	gxtkSurface* t_surf=bb_graphics_device->LoadSurface(bb_data_FixDataPath(t_path));
	DBG_LOCAL(t_surf,"surf")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<245>");
	if((t_surf)!=0){
		DBG_BLOCK();
		c_Image* t_=((new c_Image)->m_new())->p_Init(t_surf,t_frameCount,t_flags);
		return t_;
	}
	return 0;
}
c_Image* bb_graphics_LoadImage2(String t_path,int t_frameWidth,int t_frameHeight,int t_frameCount,int t_flags){
	DBG_ENTER("LoadImage")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_frameWidth,"frameWidth")
	DBG_LOCAL(t_frameHeight,"frameHeight")
	DBG_LOCAL(t_frameCount,"frameCount")
	DBG_LOCAL(t_flags,"flags")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<249>");
	gxtkSurface* t_surf=bb_graphics_device->LoadSurface(bb_data_FixDataPath(t_path));
	DBG_LOCAL(t_surf,"surf")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<250>");
	if((t_surf)!=0){
		DBG_BLOCK();
		c_Image* t_=((new c_Image)->m_new())->p_Init2(t_surf,0,0,t_frameWidth,t_frameHeight,t_frameCount,t_flags,0,0,0,t_surf->Width(),t_surf->Height());
		return t_;
	}
	return 0;
}
int bb_graphics_SetFont(c_Image* t_font,int t_firstChar){
	DBG_ENTER("SetFont")
	DBG_LOCAL(t_font,"font")
	DBG_LOCAL(t_firstChar,"firstChar")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<551>");
	if(!((t_font)!=0)){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<552>");
		if(!((bb_graphics_context->m_defaultFont)!=0)){
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<553>");
			gc_assign(bb_graphics_context->m_defaultFont,bb_graphics_LoadImage(String(L"mojo_font.png",13),96,2));
		}
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<555>");
		t_font=bb_graphics_context->m_defaultFont;
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<556>");
		t_firstChar=32;
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<558>");
	gc_assign(bb_graphics_context->m_font,t_font);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<559>");
	bb_graphics_context->m_firstChar=t_firstChar;
	return 0;
}
gxtkAudio* bb_audio_device;
int bb_audio_SetAudioDevice(gxtkAudio* t_dev){
	DBG_ENTER("SetAudioDevice")
	DBG_LOCAL(t_dev,"dev")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/audio.monkey<22>");
	gc_assign(bb_audio_device,t_dev);
	return 0;
}
c_InputDevice::c_InputDevice(){
	m__joyStates=Array<c_JoyState* >(4);
	m__keyDown=Array<bool >(512);
	m__keyHitPut=0;
	m__keyHitQueue=Array<int >(33);
	m__keyHit=Array<int >(512);
	m__charGet=0;
	m__charPut=0;
	m__charQueue=Array<int >(32);
	m__mouseX=FLOAT(.0);
	m__mouseY=FLOAT(.0);
	m__touchX=Array<Float >(32);
	m__touchY=Array<Float >(32);
	m__accelX=FLOAT(.0);
	m__accelY=FLOAT(.0);
	m__accelZ=FLOAT(.0);
}
c_InputDevice* c_InputDevice::m_new(){
	DBG_ENTER("InputDevice.new")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<26>");
	for(int t_i=0;t_i<4;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<27>");
		gc_assign(m__joyStates.At(t_i),(new c_JoyState)->m_new());
	}
	return this;
}
void c_InputDevice::p_PutKeyHit(int t_key){
	DBG_ENTER("InputDevice.PutKeyHit")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<237>");
	if(m__keyHitPut==m__keyHitQueue.Length()){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<238>");
	m__keyHit.At(t_key)+=1;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<239>");
	m__keyHitQueue.At(m__keyHitPut)=t_key;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<240>");
	m__keyHitPut+=1;
}
void c_InputDevice::p_BeginUpdate(){
	DBG_ENTER("InputDevice.BeginUpdate")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<189>");
	for(int t_i=0;t_i<4;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<190>");
		c_JoyState* t_state=m__joyStates.At(t_i);
		DBG_LOCAL(t_state,"state")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<191>");
		if(!BBGame::Game()->PollJoystick(t_i,t_state->m_joyx,t_state->m_joyy,t_state->m_joyz,t_state->m_buttons)){
			DBG_BLOCK();
			break;
		}
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<192>");
		for(int t_j=0;t_j<32;t_j=t_j+1){
			DBG_BLOCK();
			DBG_LOCAL(t_j,"j")
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<193>");
			int t_key=256+t_i*32+t_j;
			DBG_LOCAL(t_key,"key")
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<194>");
			if(t_state->m_buttons.At(t_j)){
				DBG_BLOCK();
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<195>");
				if(!m__keyDown.At(t_key)){
					DBG_BLOCK();
					DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<196>");
					m__keyDown.At(t_key)=true;
					DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<197>");
					p_PutKeyHit(t_key);
				}
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<200>");
				m__keyDown.At(t_key)=false;
			}
		}
	}
}
void c_InputDevice::p_EndUpdate(){
	DBG_ENTER("InputDevice.EndUpdate")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<207>");
	for(int t_i=0;t_i<m__keyHitPut;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<208>");
		m__keyHit.At(m__keyHitQueue.At(t_i))=0;
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<210>");
	m__keyHitPut=0;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<211>");
	m__charGet=0;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<212>");
	m__charPut=0;
}
void c_InputDevice::p_KeyEvent(int t_event,int t_data){
	DBG_ENTER("InputDevice.KeyEvent")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<111>");
	int t_1=t_event;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<112>");
	if(t_1==1){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<113>");
		if(!m__keyDown.At(t_data)){
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<114>");
			m__keyDown.At(t_data)=true;
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<115>");
			p_PutKeyHit(t_data);
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<116>");
			if(t_data==1){
				DBG_BLOCK();
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<117>");
				m__keyDown.At(384)=true;
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<118>");
				p_PutKeyHit(384);
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<119>");
				if(t_data==384){
					DBG_BLOCK();
					DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<120>");
					m__keyDown.At(1)=true;
					DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<121>");
					p_PutKeyHit(1);
				}
			}
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<124>");
		if(t_1==2){
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<125>");
			if(m__keyDown.At(t_data)){
				DBG_BLOCK();
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<126>");
				m__keyDown.At(t_data)=false;
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<127>");
				if(t_data==1){
					DBG_BLOCK();
					DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<128>");
					m__keyDown.At(384)=false;
				}else{
					DBG_BLOCK();
					DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<129>");
					if(t_data==384){
						DBG_BLOCK();
						DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<130>");
						m__keyDown.At(1)=false;
					}
				}
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<133>");
			if(t_1==3){
				DBG_BLOCK();
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<134>");
				if(m__charPut<m__charQueue.Length()){
					DBG_BLOCK();
					DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<135>");
					m__charQueue.At(m__charPut)=t_data;
					DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<136>");
					m__charPut+=1;
				}
			}
		}
	}
}
void c_InputDevice::p_MouseEvent(int t_event,int t_data,Float t_x,Float t_y){
	DBG_ENTER("InputDevice.MouseEvent")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<142>");
	int t_2=t_event;
	DBG_LOCAL(t_2,"2")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<143>");
	if(t_2==4){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<144>");
		p_KeyEvent(1,1+t_data);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<145>");
		if(t_2==5){
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<146>");
			p_KeyEvent(2,1+t_data);
			return;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<148>");
			if(t_2==6){
				DBG_BLOCK();
			}else{
				DBG_BLOCK();
				return;
			}
		}
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<152>");
	m__mouseX=t_x;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<153>");
	m__mouseY=t_y;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<154>");
	m__touchX.At(0)=t_x;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<155>");
	m__touchY.At(0)=t_y;
}
void c_InputDevice::p_TouchEvent(int t_event,int t_data,Float t_x,Float t_y){
	DBG_ENTER("InputDevice.TouchEvent")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<159>");
	int t_3=t_event;
	DBG_LOCAL(t_3,"3")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<160>");
	if(t_3==7){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<161>");
		p_KeyEvent(1,384+t_data);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<162>");
		if(t_3==8){
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<163>");
			p_KeyEvent(2,384+t_data);
			return;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<165>");
			if(t_3==9){
				DBG_BLOCK();
			}else{
				DBG_BLOCK();
				return;
			}
		}
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<169>");
	m__touchX.At(t_data)=t_x;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<170>");
	m__touchY.At(t_data)=t_y;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<171>");
	if(t_data==0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<172>");
		m__mouseX=t_x;
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<173>");
		m__mouseY=t_y;
	}
}
void c_InputDevice::p_MotionEvent(int t_event,int t_data,Float t_x,Float t_y,Float t_z){
	DBG_ENTER("InputDevice.MotionEvent")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_z,"z")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<178>");
	int t_4=t_event;
	DBG_LOCAL(t_4,"4")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<179>");
	if(t_4==10){
		DBG_BLOCK();
	}else{
		DBG_BLOCK();
		return;
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<183>");
	m__accelX=t_x;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<184>");
	m__accelY=t_y;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<185>");
	m__accelZ=t_z;
}
int c_InputDevice::p_KeyHit(int t_key){
	DBG_ENTER("InputDevice.KeyHit")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<52>");
	if(t_key>0 && t_key<512){
		DBG_BLOCK();
		return m__keyHit.At(t_key);
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<53>");
	return 0;
}
bool c_InputDevice::p_KeyDown(int t_key){
	DBG_ENTER("InputDevice.KeyDown")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<47>");
	if(t_key>0 && t_key<512){
		DBG_BLOCK();
		return m__keyDown.At(t_key);
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<48>");
	return false;
}
Float c_InputDevice::p_MouseX(){
	DBG_ENTER("InputDevice.MouseX")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<69>");
	return m__mouseX;
}
Float c_InputDevice::p_MouseY(){
	DBG_ENTER("InputDevice.MouseY")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<73>");
	return m__mouseY;
}
void c_InputDevice::mark(){
	Object::mark();
	gc_mark_q(m__joyStates);
	gc_mark_q(m__keyDown);
	gc_mark_q(m__keyHitQueue);
	gc_mark_q(m__keyHit);
	gc_mark_q(m__charQueue);
	gc_mark_q(m__touchX);
	gc_mark_q(m__touchY);
}
String c_InputDevice::debug(){
	String t="(InputDevice)\n";
	t+=dbg_decl("_keyDown",&m__keyDown);
	t+=dbg_decl("_keyHit",&m__keyHit);
	t+=dbg_decl("_keyHitQueue",&m__keyHitQueue);
	t+=dbg_decl("_keyHitPut",&m__keyHitPut);
	t+=dbg_decl("_charQueue",&m__charQueue);
	t+=dbg_decl("_charPut",&m__charPut);
	t+=dbg_decl("_charGet",&m__charGet);
	t+=dbg_decl("_mouseX",&m__mouseX);
	t+=dbg_decl("_mouseY",&m__mouseY);
	t+=dbg_decl("_touchX",&m__touchX);
	t+=dbg_decl("_touchY",&m__touchY);
	t+=dbg_decl("_accelX",&m__accelX);
	t+=dbg_decl("_accelY",&m__accelY);
	t+=dbg_decl("_accelZ",&m__accelZ);
	t+=dbg_decl("_joyStates",&m__joyStates);
	return t;
}
c_JoyState::c_JoyState(){
	m_joyx=Array<Float >(2);
	m_joyy=Array<Float >(2);
	m_joyz=Array<Float >(2);
	m_buttons=Array<bool >(32);
}
c_JoyState* c_JoyState::m_new(){
	DBG_ENTER("JoyState.new")
	c_JoyState *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/inputdevice.monkey<14>");
	return this;
}
void c_JoyState::mark(){
	Object::mark();
	gc_mark_q(m_joyx);
	gc_mark_q(m_joyy);
	gc_mark_q(m_joyz);
	gc_mark_q(m_buttons);
}
String c_JoyState::debug(){
	String t="(JoyState)\n";
	t+=dbg_decl("joyx",&m_joyx);
	t+=dbg_decl("joyy",&m_joyy);
	t+=dbg_decl("joyz",&m_joyz);
	t+=dbg_decl("buttons",&m_buttons);
	return t;
}
c_InputDevice* bb_input_device;
int bb_input_SetInputDevice(c_InputDevice* t_dev){
	DBG_ENTER("SetInputDevice")
	DBG_LOCAL(t_dev,"dev")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/input.monkey<22>");
	gc_assign(bb_input_device,t_dev);
	return 0;
}
int bb_app__devWidth;
int bb_app__devHeight;
void bb_app_ValidateDeviceWindow(bool t_notifyApp){
	DBG_ENTER("ValidateDeviceWindow")
	DBG_LOCAL(t_notifyApp,"notifyApp")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<57>");
	int t_w=bb_app__game->GetDeviceWidth();
	DBG_LOCAL(t_w,"w")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<58>");
	int t_h=bb_app__game->GetDeviceHeight();
	DBG_LOCAL(t_h,"h")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<59>");
	if(t_w==bb_app__devWidth && t_h==bb_app__devHeight){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<60>");
	bb_app__devWidth=t_w;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<61>");
	bb_app__devHeight=t_h;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<62>");
	if(t_notifyApp){
		DBG_BLOCK();
		bb_app__app->p_OnResize();
	}
}
c_DisplayMode::c_DisplayMode(){
	m__width=0;
	m__height=0;
}
c_DisplayMode* c_DisplayMode::m_new(int t_width,int t_height){
	DBG_ENTER("DisplayMode.new")
	c_DisplayMode *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_width,"width")
	DBG_LOCAL(t_height,"height")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<192>");
	m__width=t_width;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<193>");
	m__height=t_height;
	return this;
}
c_DisplayMode* c_DisplayMode::m_new2(){
	DBG_ENTER("DisplayMode.new")
	c_DisplayMode *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<189>");
	return this;
}
void c_DisplayMode::mark(){
	Object::mark();
}
String c_DisplayMode::debug(){
	String t="(DisplayMode)\n";
	t+=dbg_decl("_width",&m__width);
	t+=dbg_decl("_height",&m__height);
	return t;
}
c_Map::c_Map(){
	m_root=0;
}
c_Map* c_Map::m_new(){
	DBG_ENTER("Map.new")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<7>");
	return this;
}
c_Node* c_Map::p_FindNode(int t_key){
	DBG_ENTER("Map.FindNode")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<157>");
	c_Node* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<159>");
	while((t_node)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<160>");
		int t_cmp=p_Compare(t_key,t_node->m_key);
		DBG_LOCAL(t_cmp,"cmp")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<161>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<162>");
			t_node=t_node->m_right;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<163>");
			if(t_cmp<0){
				DBG_BLOCK();
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<164>");
				t_node=t_node->m_left;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<166>");
				return t_node;
			}
		}
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<169>");
	return t_node;
}
bool c_Map::p_Contains(int t_key){
	DBG_ENTER("Map.Contains")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<25>");
	bool t_=p_FindNode(t_key)!=0;
	return t_;
}
int c_Map::p_RotateLeft(c_Node* t_node){
	DBG_ENTER("Map.RotateLeft")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<251>");
	c_Node* t_child=t_node->m_right;
	DBG_LOCAL(t_child,"child")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<252>");
	gc_assign(t_node->m_right,t_child->m_left);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<253>");
	if((t_child->m_left)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<254>");
		gc_assign(t_child->m_left->m_parent,t_node);
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<256>");
	gc_assign(t_child->m_parent,t_node->m_parent);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<257>");
	if((t_node->m_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<258>");
		if(t_node==t_node->m_parent->m_left){
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<259>");
			gc_assign(t_node->m_parent->m_left,t_child);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<261>");
			gc_assign(t_node->m_parent->m_right,t_child);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<264>");
		gc_assign(m_root,t_child);
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<266>");
	gc_assign(t_child->m_left,t_node);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<267>");
	gc_assign(t_node->m_parent,t_child);
	return 0;
}
int c_Map::p_RotateRight(c_Node* t_node){
	DBG_ENTER("Map.RotateRight")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<271>");
	c_Node* t_child=t_node->m_left;
	DBG_LOCAL(t_child,"child")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<272>");
	gc_assign(t_node->m_left,t_child->m_right);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<273>");
	if((t_child->m_right)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<274>");
		gc_assign(t_child->m_right->m_parent,t_node);
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<276>");
	gc_assign(t_child->m_parent,t_node->m_parent);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<277>");
	if((t_node->m_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<278>");
		if(t_node==t_node->m_parent->m_right){
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<279>");
			gc_assign(t_node->m_parent->m_right,t_child);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<281>");
			gc_assign(t_node->m_parent->m_left,t_child);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<284>");
		gc_assign(m_root,t_child);
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<286>");
	gc_assign(t_child->m_right,t_node);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<287>");
	gc_assign(t_node->m_parent,t_child);
	return 0;
}
int c_Map::p_InsertFixup(c_Node* t_node){
	DBG_ENTER("Map.InsertFixup")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<212>");
	while(((t_node->m_parent)!=0) && t_node->m_parent->m_color==-1 && ((t_node->m_parent->m_parent)!=0)){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<213>");
		if(t_node->m_parent==t_node->m_parent->m_parent->m_left){
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<214>");
			c_Node* t_uncle=t_node->m_parent->m_parent->m_right;
			DBG_LOCAL(t_uncle,"uncle")
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<215>");
			if(((t_uncle)!=0) && t_uncle->m_color==-1){
				DBG_BLOCK();
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<216>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<217>");
				t_uncle->m_color=1;
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<218>");
				t_uncle->m_parent->m_color=-1;
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<219>");
				t_node=t_uncle->m_parent;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<221>");
				if(t_node==t_node->m_parent->m_right){
					DBG_BLOCK();
					DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<222>");
					t_node=t_node->m_parent;
					DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<223>");
					p_RotateLeft(t_node);
				}
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<225>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<226>");
				t_node->m_parent->m_parent->m_color=-1;
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<227>");
				p_RotateRight(t_node->m_parent->m_parent);
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<230>");
			c_Node* t_uncle2=t_node->m_parent->m_parent->m_left;
			DBG_LOCAL(t_uncle2,"uncle")
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<231>");
			if(((t_uncle2)!=0) && t_uncle2->m_color==-1){
				DBG_BLOCK();
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<232>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<233>");
				t_uncle2->m_color=1;
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<234>");
				t_uncle2->m_parent->m_color=-1;
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<235>");
				t_node=t_uncle2->m_parent;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<237>");
				if(t_node==t_node->m_parent->m_left){
					DBG_BLOCK();
					DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<238>");
					t_node=t_node->m_parent;
					DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<239>");
					p_RotateRight(t_node);
				}
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<241>");
				t_node->m_parent->m_color=1;
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<242>");
				t_node->m_parent->m_parent->m_color=-1;
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<243>");
				p_RotateLeft(t_node->m_parent->m_parent);
			}
		}
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<247>");
	m_root->m_color=1;
	return 0;
}
bool c_Map::p_Set(int t_key,c_DisplayMode* t_value){
	DBG_ENTER("Map.Set")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<29>");
	c_Node* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<30>");
	c_Node* t_parent=0;
	int t_cmp=0;
	DBG_LOCAL(t_parent,"parent")
	DBG_LOCAL(t_cmp,"cmp")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<32>");
	while((t_node)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<33>");
		t_parent=t_node;
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<34>");
		t_cmp=p_Compare(t_key,t_node->m_key);
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<35>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<36>");
			t_node=t_node->m_right;
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<37>");
			if(t_cmp<0){
				DBG_BLOCK();
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<38>");
				t_node=t_node->m_left;
			}else{
				DBG_BLOCK();
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<40>");
				gc_assign(t_node->m_value,t_value);
				DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<41>");
				return false;
			}
		}
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<45>");
	t_node=(new c_Node)->m_new(t_key,t_value,-1,t_parent);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<47>");
	if((t_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<48>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<49>");
			gc_assign(t_parent->m_right,t_node);
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<51>");
			gc_assign(t_parent->m_left,t_node);
		}
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<53>");
		p_InsertFixup(t_node);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<55>");
		gc_assign(m_root,t_node);
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<57>");
	return true;
}
bool c_Map::p_Insert(int t_key,c_DisplayMode* t_value){
	DBG_ENTER("Map.Insert")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<126>");
	bool t_=p_Set(t_key,t_value);
	return t_;
}
void c_Map::mark(){
	Object::mark();
	gc_mark_q(m_root);
}
String c_Map::debug(){
	String t="(Map)\n";
	t+=dbg_decl("root",&m_root);
	return t;
}
c_IntMap::c_IntMap(){
}
c_IntMap* c_IntMap::m_new(){
	DBG_ENTER("IntMap.new")
	c_IntMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<534>");
	c_Map::m_new();
	return this;
}
int c_IntMap::p_Compare(int t_lhs,int t_rhs){
	DBG_ENTER("IntMap.Compare")
	c_IntMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<537>");
	int t_=t_lhs-t_rhs;
	return t_;
}
void c_IntMap::mark(){
	c_Map::mark();
}
String c_IntMap::debug(){
	String t="(IntMap)\n";
	t=c_Map::debug()+t;
	return t;
}
c_Stack::c_Stack(){
	m_data=Array<c_DisplayMode* >();
	m_length=0;
}
c_Stack* c_Stack::m_new(){
	DBG_ENTER("Stack.new")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Stack* c_Stack::m_new2(Array<c_DisplayMode* > t_data){
	DBG_ENTER("Stack.new")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/stack.monkey<13>");
	gc_assign(this->m_data,t_data.Slice(0));
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/stack.monkey<14>");
	this->m_length=t_data.Length();
	return this;
}
void c_Stack::p_Push(c_DisplayMode* t_value){
	DBG_ENTER("Stack.Push")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/stack.monkey<67>");
	if(m_length==m_data.Length()){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/stack.monkey<68>");
		gc_assign(m_data,m_data.Resize(m_length*2+10));
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/stack.monkey<70>");
	gc_assign(m_data.At(m_length),t_value);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/stack.monkey<71>");
	m_length+=1;
}
void c_Stack::p_Push2(Array<c_DisplayMode* > t_values,int t_offset,int t_count){
	DBG_ENTER("Stack.Push")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/stack.monkey<79>");
	for(int t_i=0;t_i<t_count;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/stack.monkey<80>");
		p_Push(t_values.At(t_offset+t_i));
	}
}
void c_Stack::p_Push3(Array<c_DisplayMode* > t_values,int t_offset){
	DBG_ENTER("Stack.Push")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/stack.monkey<75>");
	p_Push2(t_values,t_offset,t_values.Length()-t_offset);
}
Array<c_DisplayMode* > c_Stack::p_ToArray(){
	DBG_ENTER("Stack.ToArray")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/stack.monkey<18>");
	Array<c_DisplayMode* > t_t=Array<c_DisplayMode* >(m_length);
	DBG_LOCAL(t_t,"t")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/stack.monkey<19>");
	for(int t_i=0;t_i<m_length;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/stack.monkey<20>");
		gc_assign(t_t.At(t_i),m_data.At(t_i));
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/stack.monkey<22>");
	return t_t;
}
void c_Stack::mark(){
	Object::mark();
	gc_mark_q(m_data);
}
String c_Stack::debug(){
	String t="(Stack)\n";
	t+=dbg_decl("data",&m_data);
	t+=dbg_decl("length",&m_length);
	return t;
}
c_Node::c_Node(){
	m_key=0;
	m_right=0;
	m_left=0;
	m_value=0;
	m_color=0;
	m_parent=0;
}
c_Node* c_Node::m_new(int t_key,c_DisplayMode* t_value,int t_color,c_Node* t_parent){
	DBG_ENTER("Node.new")
	c_Node *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_LOCAL(t_color,"color")
	DBG_LOCAL(t_parent,"parent")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<364>");
	this->m_key=t_key;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<365>");
	gc_assign(this->m_value,t_value);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<366>");
	this->m_color=t_color;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<367>");
	gc_assign(this->m_parent,t_parent);
	return this;
}
c_Node* c_Node::m_new2(){
	DBG_ENTER("Node.new")
	c_Node *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/map.monkey<361>");
	return this;
}
void c_Node::mark(){
	Object::mark();
	gc_mark_q(m_right);
	gc_mark_q(m_left);
	gc_mark_q(m_value);
	gc_mark_q(m_parent);
}
String c_Node::debug(){
	String t="(Node)\n";
	t+=dbg_decl("key",&m_key);
	t+=dbg_decl("value",&m_value);
	t+=dbg_decl("color",&m_color);
	t+=dbg_decl("parent",&m_parent);
	t+=dbg_decl("left",&m_left);
	t+=dbg_decl("right",&m_right);
	return t;
}
Array<c_DisplayMode* > bb_app__displayModes;
c_DisplayMode* bb_app__desktopMode;
int bb_app_DeviceWidth(){
	DBG_ENTER("DeviceWidth")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<263>");
	return bb_app__devWidth;
}
int bb_app_DeviceHeight(){
	DBG_ENTER("DeviceHeight")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<267>");
	return bb_app__devHeight;
}
void bb_app_EnumDisplayModes(){
	DBG_ENTER("EnumDisplayModes")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<33>");
	Array<BBDisplayMode* > t_modes=bb_app__game->GetDisplayModes();
	DBG_LOCAL(t_modes,"modes")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<34>");
	c_IntMap* t_mmap=(new c_IntMap)->m_new();
	DBG_LOCAL(t_mmap,"mmap")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<35>");
	c_Stack* t_mstack=(new c_Stack)->m_new();
	DBG_LOCAL(t_mstack,"mstack")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<36>");
	for(int t_i=0;t_i<t_modes.Length();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<37>");
		int t_w=t_modes.At(t_i)->width;
		DBG_LOCAL(t_w,"w")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<38>");
		int t_h=t_modes.At(t_i)->height;
		DBG_LOCAL(t_h,"h")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<39>");
		int t_size=t_w<<16|t_h;
		DBG_LOCAL(t_size,"size")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<40>");
		if(t_mmap->p_Contains(t_size)){
			DBG_BLOCK();
		}else{
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<42>");
			c_DisplayMode* t_mode=(new c_DisplayMode)->m_new(t_modes.At(t_i)->width,t_modes.At(t_i)->height);
			DBG_LOCAL(t_mode,"mode")
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<43>");
			t_mmap->p_Insert(t_size,t_mode);
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<44>");
			t_mstack->p_Push(t_mode);
		}
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<47>");
	gc_assign(bb_app__displayModes,t_mstack->p_ToArray());
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<48>");
	BBDisplayMode* t_mode2=bb_app__game->GetDesktopMode();
	DBG_LOCAL(t_mode2,"mode")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<49>");
	if((t_mode2)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<50>");
		gc_assign(bb_app__desktopMode,(new c_DisplayMode)->m_new(t_mode2->width,t_mode2->height));
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<52>");
		gc_assign(bb_app__desktopMode,(new c_DisplayMode)->m_new(bb_app_DeviceWidth(),bb_app_DeviceHeight()));
	}
}
gxtkGraphics* bb_graphics_renderDevice;
int bb_graphics_SetMatrix(Float t_ix,Float t_iy,Float t_jx,Float t_jy,Float t_tx,Float t_ty){
	DBG_ENTER("SetMatrix")
	DBG_LOCAL(t_ix,"ix")
	DBG_LOCAL(t_iy,"iy")
	DBG_LOCAL(t_jx,"jx")
	DBG_LOCAL(t_jy,"jy")
	DBG_LOCAL(t_tx,"tx")
	DBG_LOCAL(t_ty,"ty")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<317>");
	bb_graphics_context->m_ix=t_ix;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<318>");
	bb_graphics_context->m_iy=t_iy;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<319>");
	bb_graphics_context->m_jx=t_jx;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<320>");
	bb_graphics_context->m_jy=t_jy;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<321>");
	bb_graphics_context->m_tx=t_tx;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<322>");
	bb_graphics_context->m_ty=t_ty;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<323>");
	bb_graphics_context->m_tformed=((t_ix!=FLOAT(1.0) || t_iy!=FLOAT(0.0) || t_jx!=FLOAT(0.0) || t_jy!=FLOAT(1.0) || t_tx!=FLOAT(0.0) || t_ty!=FLOAT(0.0))?1:0);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<324>");
	bb_graphics_context->m_matDirty=1;
	return 0;
}
int bb_graphics_SetMatrix2(Array<Float > t_m){
	DBG_ENTER("SetMatrix")
	DBG_LOCAL(t_m,"m")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<313>");
	bb_graphics_SetMatrix(t_m.At(0),t_m.At(1),t_m.At(2),t_m.At(3),t_m.At(4),t_m.At(5));
	return 0;
}
int bb_graphics_SetColor(Float t_r,Float t_g,Float t_b){
	DBG_ENTER("SetColor")
	DBG_LOCAL(t_r,"r")
	DBG_LOCAL(t_g,"g")
	DBG_LOCAL(t_b,"b")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<259>");
	bb_graphics_context->m_color_r=t_r;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<260>");
	bb_graphics_context->m_color_g=t_g;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<261>");
	bb_graphics_context->m_color_b=t_b;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<262>");
	bb_graphics_renderDevice->SetColor(t_r,t_g,t_b);
	return 0;
}
int bb_graphics_SetAlpha(Float t_alpha){
	DBG_ENTER("SetAlpha")
	DBG_LOCAL(t_alpha,"alpha")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<276>");
	bb_graphics_context->m_alpha=t_alpha;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<277>");
	bb_graphics_renderDevice->SetAlpha(t_alpha);
	return 0;
}
int bb_graphics_SetBlend(int t_blend){
	DBG_ENTER("SetBlend")
	DBG_LOCAL(t_blend,"blend")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<285>");
	bb_graphics_context->m_blend=t_blend;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<286>");
	bb_graphics_renderDevice->SetBlend(t_blend);
	return 0;
}
int bb_graphics_SetScissor(Float t_x,Float t_y,Float t_width,Float t_height){
	DBG_ENTER("SetScissor")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_width,"width")
	DBG_LOCAL(t_height,"height")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<294>");
	bb_graphics_context->m_scissor_x=t_x;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<295>");
	bb_graphics_context->m_scissor_y=t_y;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<296>");
	bb_graphics_context->m_scissor_width=t_width;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<297>");
	bb_graphics_context->m_scissor_height=t_height;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<298>");
	bb_graphics_renderDevice->SetScissor(int(t_x),int(t_y),int(t_width),int(t_height));
	return 0;
}
int bb_graphics_BeginRender(){
	DBG_ENTER("BeginRender")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<220>");
	gc_assign(bb_graphics_renderDevice,bb_graphics_device);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<221>");
	bb_graphics_context->m_matrixSp=0;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<222>");
	bb_graphics_SetMatrix(FLOAT(1.0),FLOAT(0.0),FLOAT(0.0),FLOAT(1.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<223>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<224>");
	bb_graphics_SetAlpha(FLOAT(1.0));
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<225>");
	bb_graphics_SetBlend(0);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<226>");
	bb_graphics_SetScissor(FLOAT(0.0),FLOAT(0.0),Float(bb_app_DeviceWidth()),Float(bb_app_DeviceHeight()));
	return 0;
}
int bb_graphics_EndRender(){
	DBG_ENTER("EndRender")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<230>");
	bb_graphics_renderDevice=0;
	return 0;
}
c_BBGameEvent::c_BBGameEvent(){
}
void c_BBGameEvent::mark(){
	Object::mark();
}
String c_BBGameEvent::debug(){
	String t="(BBGameEvent)\n";
	return t;
}
void bb_app_EndApp(){
	DBG_ENTER("EndApp")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<259>");
	bbError(String());
}
void bb_app_SetDeviceWindow(int t_width,int t_height,int t_flags){
	DBG_ENTER("SetDeviceWindow")
	DBG_LOCAL(t_width,"width")
	DBG_LOCAL(t_height,"height")
	DBG_LOCAL(t_flags,"flags")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<271>");
	bb_app__game->SetDeviceWindow(t_width,t_height,t_flags);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<272>");
	bb_app_ValidateDeviceWindow(false);
}
int bb_app__updateRate;
void bb_app_SetUpdateRate(int t_hertz){
	DBG_ENTER("SetUpdateRate")
	DBG_LOCAL(t_hertz,"hertz")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<224>");
	bb_app__updateRate=t_hertz;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<225>");
	bb_app__game->SetUpdateRate(t_hertz);
}
int bb_audio_SetMusicVolume(Float t_volume){
	DBG_ENTER("SetMusicVolume")
	DBG_LOCAL(t_volume,"volume")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/audio.monkey<105>");
	bb_audio_device->SetMusicVolume(t_volume);
	return 0;
}
c_GameObject::c_GameObject(){
	m_x=FLOAT(.0);
	m_y=FLOAT(.0);
	m_width=0;
	m_height=0;
	m_path=String();
	m_sprite=0;
}
c_GameObject* c_GameObject::m_new(Float t__x,Float t__y,int t__width,int t__height,String t__path){
	DBG_ENTER("GameObject.new")
	c_GameObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__x,"_x")
	DBG_LOCAL(t__y,"_y")
	DBG_LOCAL(t__width,"_width")
	DBG_LOCAL(t__height,"_height")
	DBG_LOCAL(t__path,"_path")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<62>");
	m_x=t__x;
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<63>");
	m_y=t__y;
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<64>");
	m_width=t__width;
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<65>");
	m_height=t__height;
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<66>");
	m_path=t__path;
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<67>");
	gc_assign(m_sprite,bb_graphics_LoadImage(m_path,m_width,m_height));
	return this;
}
c_GameObject* c_GameObject::m_new2(){
	DBG_ENTER("GameObject.new")
	c_GameObject *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<53>");
	return this;
}
void c_GameObject::mark(){
	Object::mark();
	gc_mark_q(m_sprite);
}
String c_GameObject::debug(){
	String t="(GameObject)\n";
	t+=dbg_decl("x",&m_x);
	t+=dbg_decl("y",&m_y);
	t+=dbg_decl("width",&m_width);
	t+=dbg_decl("height",&m_height);
	t+=dbg_decl("path",&m_path);
	t+=dbg_decl("sprite",&m_sprite);
	return t;
}
c_Button::c_Button(){
	m_action=String();
	m_active=false;
}
c_Button* c_Button::m_new(String t__path){
	DBG_ENTER("Button.new")
	c_Button *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__path,"_path")
	DBG_INFO("F:/Final Game/Ragnarok.monkey<217>");
	c_GameObject::m_new2();
	DBG_INFO("F:/Final Game/Ragnarok.monkey<218>");
	m_width=320;
	DBG_INFO("F:/Final Game/Ragnarok.monkey<219>");
	m_height=50;
	DBG_INFO("F:/Final Game/Ragnarok.monkey<220>");
	m_path=t__path;
	DBG_INFO("F:/Final Game/Ragnarok.monkey<221>");
	gc_assign(m_sprite,bb_graphics_LoadImage(m_path,1,c_Image::m_DefaultFlags));
	return this;
}
c_Button* c_Button::m_new2(){
	DBG_ENTER("Button.new")
	c_Button *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/Ragnarok.monkey<213>");
	c_GameObject::m_new2();
	return this;
}
int c_Button::p_SetAction(String t__action){
	DBG_ENTER("Button.SetAction")
	c_Button *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__action,"_action")
	DBG_INFO("F:/Final Game/Ragnarok.monkey<225>");
	m_action=t__action;
	return 0;
}
int c_Button::p_SetActive(bool t__active){
	DBG_ENTER("Button.SetActive")
	c_Button *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__active,"_active")
	DBG_INFO("F:/Final Game/Ragnarok.monkey<229>");
	m_active=t__active;
	return 0;
}
String c_Button::p_GetAction(){
	DBG_ENTER("Button.GetAction")
	c_Button *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/Ragnarok.monkey<233>");
	return m_action;
}
int c_Button::p_Draw(int t__x,int t__y){
	DBG_ENTER("Button.Draw")
	c_Button *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__x,"_x")
	DBG_LOCAL(t__y,"_y")
	DBG_INFO("F:/Final Game/Ragnarok.monkey<237>");
	if(m_active==true){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/Ragnarok.monkey<238>");
		bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
		DBG_INFO("F:/Final Game/Ragnarok.monkey<239>");
		bb_graphics_DrawRect(Float(t__x-5),Float(t__y-5),Float(m_width+10),Float(m_height+10));
	}
	DBG_INFO("F:/Final Game/Ragnarok.monkey<241>");
	bb_graphics_DrawImage(m_sprite,Float(t__x),Float(t__y),0);
	return 0;
}
void c_Button::mark(){
	c_GameObject::mark();
}
String c_Button::debug(){
	String t="(Button)\n";
	t=c_GameObject::debug()+t;
	t+=dbg_decl("active",&m_active);
	t+=dbg_decl("action",&m_action);
	return t;
}
c_VolumeSlider::c_VolumeSlider(){
	m_dialNum=0;
	m_volPercent=FLOAT(.0);
}
c_VolumeSlider* c_VolumeSlider::m_new(int t__dialNum,int t__x,int t__y,int t__width,int t__height){
	DBG_ENTER("VolumeSlider.new")
	c_VolumeSlider *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__dialNum,"_dialNum")
	DBG_LOCAL(t__x,"_x")
	DBG_LOCAL(t__y,"_y")
	DBG_LOCAL(t__width,"_width")
	DBG_LOCAL(t__height,"_height")
	DBG_INFO("F:/Final Game/Ragnarok.monkey<251>");
	c_GameObject::m_new2();
	DBG_INFO("F:/Final Game/Ragnarok.monkey<252>");
	m_dialNum=t__dialNum;
	DBG_INFO("F:/Final Game/Ragnarok.monkey<253>");
	m_x=Float(t__x);
	DBG_INFO("F:/Final Game/Ragnarok.monkey<254>");
	m_y=Float(t__y);
	DBG_INFO("F:/Final Game/Ragnarok.monkey<255>");
	m_width=t__width;
	DBG_INFO("F:/Final Game/Ragnarok.monkey<256>");
	m_height=t__height;
	DBG_INFO("F:/Final Game/Ragnarok.monkey<257>");
	m_volPercent=FLOAT(1.0);
	return this;
}
c_VolumeSlider* c_VolumeSlider::m_new2(){
	DBG_ENTER("VolumeSlider.new")
	c_VolumeSlider *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/Ragnarok.monkey<247>");
	c_GameObject::m_new2();
	return this;
}
int c_VolumeSlider::p_updateVol(){
	DBG_ENTER("VolumeSlider.updateVol")
	c_VolumeSlider *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/Ragnarok.monkey<267>");
	if(((bb_input_KeyHit(65))!=0) && m_volPercent>FLOAT(0.0)){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/Ragnarok.monkey<268>");
		m_volPercent-=FLOAT(0.1);
	}
	DBG_INFO("F:/Final Game/Ragnarok.monkey<270>");
	if(((bb_input_KeyHit(68))!=0) && m_volPercent<FLOAT(1.0)){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/Ragnarok.monkey<271>");
		m_volPercent+=FLOAT(0.1);
	}
	return 0;
}
Float c_VolumeSlider::p_getVol(){
	DBG_ENTER("VolumeSlider.getVol")
	c_VolumeSlider *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/Ragnarok.monkey<276>");
	return m_volPercent;
}
int c_VolumeSlider::p_Draw2(){
	DBG_ENTER("VolumeSlider.Draw")
	c_VolumeSlider *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/Ragnarok.monkey<261>");
	for(int t_i=0;Float(t_i)<=Float(m_dialNum)*m_volPercent-FLOAT(1.0);t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("F:/Final Game/Ragnarok.monkey<262>");
		bb_graphics_DrawRect(m_x+Float(t_i*m_width*2),m_y,Float(m_width),Float(m_height));
	}
	return 0;
}
void c_VolumeSlider::mark(){
	c_GameObject::mark();
}
String c_VolumeSlider::debug(){
	String t="(VolumeSlider)\n";
	t=c_GameObject::debug()+t;
	t+=dbg_decl("dialNum",&m_dialNum);
	t+=dbg_decl("volPercent",&m_volPercent);
	return t;
}
int bb_audio_PlayMusic(String t_path,int t_flags){
	DBG_ENTER("PlayMusic")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_flags,"flags")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/audio.monkey<85>");
	int t_=bb_audio_device->PlayMusic(bb_data_FixDataPath(t_path),t_flags);
	return t_;
}
c_Crosshair::c_Crosshair(){
}
c_Crosshair* c_Crosshair::m_new(int t_type){
	DBG_ENTER("Crosshair.new")
	c_Crosshair *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_type,"type")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<74>");
	c_GameObject::m_new2();
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<75>");
	gc_assign(m_sprite,bb_graphics_LoadImage(String(L"gui/xhairs/",11)+String(t_type)+String(L".png",4),1,c_Image::m_DefaultFlags));
	return this;
}
c_Crosshair* c_Crosshair::m_new2(){
	DBG_ENTER("Crosshair.new")
	c_Crosshair *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<72>");
	c_GameObject::m_new2();
	return this;
}
int c_Crosshair::p_Draw2(){
	DBG_ENTER("Crosshair.Draw")
	c_Crosshair *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<79>");
	bb_graphics_DrawImage(m_sprite,bb_input_MouseX()-FLOAT(7.0),bb_input_MouseY()-FLOAT(7.0),0);
	return 0;
}
void c_Crosshair::mark(){
	c_GameObject::mark();
}
String c_Crosshair::debug(){
	String t="(Crosshair)\n";
	t=c_GameObject::debug()+t;
	return t;
}
c_Player::c_Player(){
	m_speed=0;
	m_idleR=0;
	m_idleL=0;
	m_right=0;
	m_left=0;
	m_heartEmptySprite=0;
	m_heartFullSprite=0;
	m_maxHealth=0;
	m_curHealth=0;
	m_hurtSound=0;
	m_curWeapon=0;
	m_itemArray=Array<c_Interactables* >(2);
	m_centreX=FLOAT(.0);
	m_centreY=FLOAT(.0);
	m_uber=false;
	m_uberCD=500;
	m_uberTime=0;
	m_movementVec=(new c_Vec2D)->m_new(FLOAT(0.0),FLOAT(0.0));
	m_animState=String(L"RIGHT",5);
}
c_Player* c_Player::m_new(Float t__x,Float t__y,int t__speed,int t__width,int t__height){
	DBG_ENTER("Player.new")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__x,"_x")
	DBG_LOCAL(t__y,"_y")
	DBG_LOCAL(t__speed,"_speed")
	DBG_LOCAL(t__width,"_width")
	DBG_LOCAL(t__height,"_height")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<28>");
	c_GameObject::m_new2();
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<29>");
	m_x=t__x;
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<30>");
	m_y=t__y;
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<31>");
	m_speed=t__speed;
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<32>");
	m_width=t__width;
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<33>");
	m_height=t__height;
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<34>");
	gc_assign(m_idleR,(new c_Animation)->m_new(String(L"characters/characterIdleRight.png",33),16,24,4,100,FLOAT(2.0),FLOAT(2.0)));
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<35>");
	gc_assign(m_idleL,(new c_Animation)->m_new(String(L"characters/characterIdleLeft.png",32),16,24,4,100,FLOAT(2.0),FLOAT(2.0)));
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<36>");
	gc_assign(m_right,(new c_Animation)->m_new(String(L"characters/characterRunRight.png",32),16,24,4,100,FLOAT(2.0),FLOAT(2.0)));
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<37>");
	gc_assign(m_left,(new c_Animation)->m_new(String(L"characters/characterRunLeft.png",31),16,24,4,100,FLOAT(2.0),FLOAT(2.0)));
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<38>");
	gc_assign(m_heartEmptySprite,bb_graphics_LoadImage(String(L"gui/ui_heart_empty.png",22),1,c_Image::m_DefaultFlags));
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<39>");
	gc_assign(m_heartFullSprite,bb_graphics_LoadImage(String(L"gui/ui_heart_full.png",21),1,c_Image::m_DefaultFlags));
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<40>");
	m_maxHealth=3;
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<41>");
	m_curHealth=3;
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<42>");
	gc_assign(m_hurtSound,bb_audio_LoadSound(String(L"soundfx/hurt.wav",16)));
	return this;
}
c_Player* c_Player::m_new2(){
	DBG_ENTER("Player.new")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<6>");
	c_GameObject::m_new2();
	return this;
}
int c_Player::p_Reset(){
	DBG_ENTER("Player.Reset")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<228>");
	m_x=FLOAT(200.0);
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<229>");
	m_y=FLOAT(200.0);
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<230>");
	m_curHealth=3;
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<231>");
	m_maxHealth=3;
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<232>");
	m_curWeapon=0;
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<233>");
	m_itemArray.At(0)=0;
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<234>");
	m_itemArray.At(1)=0;
	return 0;
}
Float c_Player::p_GetCentreX(){
	DBG_ENTER("Player.GetCentreX")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<199>");
	return m_centreX;
}
Float c_Player::p_GetCentreY(){
	DBG_ENTER("Player.GetCentreY")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<203>");
	return m_centreY;
}
Float c_Player::p_GetX(){
	DBG_ENTER("Player.GetX")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<119>");
	Float t_=m_x+FLOAT(6.0);
	return t_;
}
Float c_Player::p_GetY(){
	DBG_ENTER("Player.GetY")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<123>");
	Float t_=m_y+FLOAT(18.0);
	return t_;
}
int c_Player::p_GetWidth(){
	DBG_ENTER("Player.GetWidth")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<191>");
	return 10;
}
int c_Player::p_GetHeight(){
	DBG_ENTER("Player.GetHeight")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<195>");
	return 15;
}
int c_Player::p_Hurt(){
	DBG_ENTER("Player.Hurt")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<52>");
	if(m_curHealth>0 && m_uber==false){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<53>");
		m_curHealth-=1;
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<54>");
		m_uber=true;
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<55>");
		m_uberTime=bb_app_Millisecs()+m_uberCD;
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<56>");
		bb_audio_PlaySound(m_hurtSound,0,0);
	}
	return 0;
}
int c_Player::p_GetCurHealth(){
	DBG_ENTER("Player.GetCurHealth")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<212>");
	return m_curHealth;
}
int c_Player::p_Update(){
	DBG_ENTER("Player.Update")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<78>");
	m_movementVec->p_Reset();
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<79>");
	if((bb_input_KeyDown(87))!=0){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<80>");
		m_movementVec->p_UpdateY2(FLOAT(-1.0));
	}
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<82>");
	if((bb_input_KeyDown(83))!=0){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<83>");
		m_movementVec->p_UpdateY2(FLOAT(1.0));
	}
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<85>");
	if((bb_input_KeyDown(65))!=0){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<86>");
		m_movementVec->p_UpdateX2(FLOAT(-1.0));
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<87>");
		m_animState=String(L"LEFT",4);
	}
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<89>");
	if((bb_input_KeyDown(68))!=0){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<90>");
		m_movementVec->p_UpdateX2(FLOAT(1.0));
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<91>");
		m_animState=String(L"RIGHT",5);
	}
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<93>");
	m_movementVec->p_Normalise();
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<98>");
	if(bb_app_Millisecs()>=m_uberTime && m_uber==true){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<99>");
		m_uber=false;
	}
	return 0;
}
int c_Player::p_UpdateX(){
	DBG_ENTER("Player.UpdateX")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<105>");
	if(m_movementVec->p_GetMagnitude()!=FLOAT(0.0)){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<106>");
		m_x+=m_movementVec->p_GetX()*Float(m_speed);
	}
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<108>");
	m_centreX=m_x+FLOAT(16.0);
	return 0;
}
int c_Player::p_UseItem1(c_Player* t_player){
	DBG_ENTER("Player.UseItem1")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_player,"player")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<67>");
	if(((bb_input_KeyHit(81))!=0) && m_itemArray.At(0)!=0){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<68>");
		m_itemArray.At(0)->p_Use(t_player);
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<69>");
		m_itemArray.At(0)=0;
	}
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<71>");
	if(((bb_input_KeyHit(69))!=0) && m_itemArray.At(1)!=0){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<72>");
		m_itemArray.At(1)->p_Use(t_player);
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<73>");
		m_itemArray.At(1)=0;
	}
	return 0;
}
int c_Player::p_CollRespX(){
	DBG_ENTER("Player.CollRespX")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<127>");
	if(m_movementVec->p_GetMagnitude()!=FLOAT(0.0)){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<128>");
		m_x+=-(m_movementVec->p_GetX()*Float(m_speed));
	}
	return 0;
}
int c_Player::p_UpdateY(){
	DBG_ENTER("Player.UpdateY")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<112>");
	if(m_movementVec->p_GetMagnitude()!=FLOAT(0.0)){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<113>");
		m_y+=m_movementVec->p_GetY()*Float(m_speed);
	}
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<115>");
	m_centreY=m_y+FLOAT(30.0);
	return 0;
}
int c_Player::p_CollRespY(){
	DBG_ENTER("Player.CollRespY")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<133>");
	if(m_movementVec->p_GetMagnitude()!=FLOAT(0.0)){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<134>");
		m_y+=-(m_movementVec->p_GetY()*Float(m_speed));
	}
	return 0;
}
int c_Player::p_SetX(Float t__x){
	DBG_ENTER("Player.SetX")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__x,"_x")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<179>");
	m_x=t__x;
	return 0;
}
int c_Player::p_SetY(Float t__y){
	DBG_ENTER("Player.SetY")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__y,"_y")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<183>");
	m_y=t__y;
	return 0;
}
int c_Player::p_AddItem(c_Interactables* t_item){
	DBG_ENTER("Player.AddItem")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_item,"item")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<216>");
	if(m_itemArray.At(0)==0){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<217>");
		gc_assign(m_itemArray.At(0),t_item);
	}else{
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<218>");
		if(m_itemArray.At(1)==0){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<219>");
			gc_assign(m_itemArray.At(1),t_item);
		}
	}
	return 0;
}
int c_Player::p_AddWeapon(c_WeaponInterface* t_weapon){
	DBG_ENTER("Player.AddWeapon")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_weapon,"weapon")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<224>");
	gc_assign(m_curWeapon,t_weapon);
	return 0;
}
int c_Player::p_Attack(c_List3* t_target){
	DBG_ENTER("Player.Attack")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_target,"target")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<46>");
	if(m_curWeapon!=0){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<47>");
		m_curWeapon->p_Attack2(int(m_x),int(m_y),t_target);
	}
	return 0;
}
int c_Player::p_Draw2(){
	DBG_ENTER("Player.Draw")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<139>");
	if(m_uber==true){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<140>");
		bb_graphics_SetColor(FLOAT(255.0),FLOAT(0.0),FLOAT(0.0));
	}else{
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<142>");
		bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	}
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<144>");
	String t_1=m_animState;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<145>");
	if(t_1==String(L"RIGHT",5)){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<146>");
		if(String(m_movementVec->p_GetX())==String(L"NaN",3) && String(m_movementVec->p_GetY())==String(L"NaN",3)){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<147>");
			m_idleR->p_Play(m_x,m_y);
		}else{
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<149>");
			m_right->p_Play(m_x,m_y);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<151>");
		if(t_1==String(L"LEFT",4)){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<152>");
			if(String(m_movementVec->p_GetX())==String(L"NaN",3) && String(m_movementVec->p_GetY())==String(L"NaN",3)){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<153>");
				m_idleL->p_Play(m_x,m_y);
			}else{
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<155>");
				m_left->p_Play(m_x,m_y);
			}
		}
	}
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<159>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<161>");
	for(int t_i=0;t_i<=m_maxHealth-1;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<162>");
		bb_graphics_DrawImage2(m_heartEmptySprite,Float(500+t_i*48),FLOAT(400.0),FLOAT(0.0),FLOAT(2.0),FLOAT(2.0),0);
	}
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<164>");
	for(int t_i2=0;t_i2<=m_curHealth-1;t_i2=t_i2+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i2,"i")
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<165>");
		bb_graphics_DrawImage2(m_heartFullSprite,Float(500+t_i2*48),FLOAT(400.0),FLOAT(0.0),FLOAT(2.0),FLOAT(2.0),0);
	}
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<167>");
	if(m_curWeapon!=0){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<168>");
		m_curWeapon->p_Draw(int(m_x),int(m_y));
	}
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<170>");
	for(int t_i3=0;t_i3<=m_itemArray.Length()-1;t_i3=t_i3+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i3,"i")
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<171>");
		if(m_itemArray.At(t_i3)!=0){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<172>");
			m_itemArray.At(t_i3)->p_Draw3(t_i3);
		}
	}
	return 0;
}
int c_Player::p_Heal(){
	DBG_ENTER("Player.Heal")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<61>");
	if(m_curHealth<m_maxHealth){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/UserPlayer.monkey<62>");
		m_curHealth+=1;
	}
	return 0;
}
void c_Player::mark(){
	c_GameObject::mark();
	gc_mark_q(m_idleR);
	gc_mark_q(m_idleL);
	gc_mark_q(m_right);
	gc_mark_q(m_left);
	gc_mark_q(m_heartEmptySprite);
	gc_mark_q(m_heartFullSprite);
	gc_mark_q(m_hurtSound);
	gc_mark_q(m_curWeapon);
	gc_mark_q(m_itemArray);
	gc_mark_q(m_movementVec);
}
String c_Player::debug(){
	String t="(Player)\n";
	t=c_GameObject::debug()+t;
	t+=dbg_decl("speed",&m_speed);
	t+=dbg_decl("animState",&m_animState);
	t+=dbg_decl("idleR",&m_idleR);
	t+=dbg_decl("idleL",&m_idleL);
	t+=dbg_decl("right",&m_right);
	t+=dbg_decl("left",&m_left);
	t+=dbg_decl("movementVec",&m_movementVec);
	t+=dbg_decl("maxHealth",&m_maxHealth);
	t+=dbg_decl("curHealth",&m_curHealth);
	t+=dbg_decl("heartEmptySprite",&m_heartEmptySprite);
	t+=dbg_decl("heartFullSprite",&m_heartFullSprite);
	t+=dbg_decl("itemArray",&m_itemArray);
	t+=dbg_decl("curWeapon",&m_curWeapon);
	t+=dbg_decl("centreX",&m_centreX);
	t+=dbg_decl("centreY",&m_centreY);
	t+=dbg_decl("uberCD",&m_uberCD);
	t+=dbg_decl("uber",&m_uber);
	t+=dbg_decl("uberTime",&m_uberTime);
	t+=dbg_decl("hurtSound",&m_hurtSound);
	return t;
}
c_Animation::c_Animation(){
	m_spriteNum=0;
	m_frameTime=0;
	m_xScale=FLOAT(.0);
	m_yScale=FLOAT(.0);
}
c_Animation* c_Animation::m_new(String t__path,int t__width,int t__height,int t__spriteNum,int t__frameTime,Float t__xScale,Float t__yScale){
	DBG_ENTER("Animation.new")
	c_Animation *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__path,"_path")
	DBG_LOCAL(t__width,"_width")
	DBG_LOCAL(t__height,"_height")
	DBG_LOCAL(t__spriteNum,"_spriteNum")
	DBG_LOCAL(t__frameTime,"_frameTime")
	DBG_LOCAL(t__xScale,"_xScale")
	DBG_LOCAL(t__yScale,"_yScale")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<132>");
	c_GameObject::m_new2();
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<133>");
	m_spriteNum=t__spriteNum;
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<134>");
	m_frameTime=t__frameTime;
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<135>");
	m_width=t__width;
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<136>");
	m_height=t__height;
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<137>");
	m_path=t__path;
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<138>");
	m_xScale=t__xScale;
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<139>");
	m_yScale=t__yScale;
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<140>");
	gc_assign(m_sprite,bb_graphics_LoadImage2(m_path,m_width,m_height,m_spriteNum,c_Image::m_DefaultFlags));
	return this;
}
c_Animation* c_Animation::m_new2(){
	DBG_ENTER("Animation.new")
	c_Animation *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<126>");
	c_GameObject::m_new2();
	return this;
}
int c_Animation::p_Play(Float t_x,Float t_y){
	DBG_ENTER("Animation.Play")
	c_Animation *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<144>");
	int t_frameCounter=bb_app_Millisecs() % (m_spriteNum*m_frameTime)/m_frameTime;
	DBG_LOCAL(t_frameCounter,"frameCounter")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<145>");
	bb_graphics_DrawImage2(m_sprite,t_x,t_y,FLOAT(0.0),m_xScale,m_yScale,t_frameCounter);
	return 0;
}
int c_Animation::p_Play2(Float t_x,Float t_y,int t_rotation){
	DBG_ENTER("Animation.Play")
	c_Animation *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_rotation,"rotation")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<149>");
	int t_frameCounter=bb_app_Millisecs() % (m_spriteNum*m_frameTime)/m_frameTime;
	DBG_LOCAL(t_frameCounter,"frameCounter")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<150>");
	bb_graphics_DrawImage2(m_sprite,t_x,t_y,Float(t_rotation),m_xScale,m_yScale,t_frameCounter);
	return 0;
}
void c_Animation::mark(){
	c_GameObject::mark();
}
String c_Animation::debug(){
	String t="(Animation)\n";
	t=c_GameObject::debug()+t;
	t+=dbg_decl("spriteNum",&m_spriteNum);
	t+=dbg_decl("frameTime",&m_frameTime);
	t+=dbg_decl("xScale",&m_xScale);
	t+=dbg_decl("yScale",&m_yScale);
	return t;
}
c_Sound::c_Sound(){
	m_sample=0;
}
c_Sound* c_Sound::m_new(gxtkSample* t_sample){
	DBG_ENTER("Sound.new")
	c_Sound *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_sample,"sample")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/audio.monkey<32>");
	gc_assign(this->m_sample,t_sample);
	return this;
}
c_Sound* c_Sound::m_new2(){
	DBG_ENTER("Sound.new")
	c_Sound *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/audio.monkey<29>");
	return this;
}
void c_Sound::mark(){
	Object::mark();
	gc_mark_q(m_sample);
}
String c_Sound::debug(){
	String t="(Sound)\n";
	t+=dbg_decl("sample",&m_sample);
	return t;
}
c_Sound* bb_audio_LoadSound(String t_path){
	DBG_ENTER("LoadSound")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/audio.monkey<47>");
	gxtkSample* t_sample=bb_audio_device->LoadSample(bb_data_FixDataPath(t_path));
	DBG_LOCAL(t_sample,"sample")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/audio.monkey<48>");
	if((t_sample)!=0){
		DBG_BLOCK();
		c_Sound* t_=(new c_Sound)->m_new(t_sample);
		return t_;
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/audio.monkey<49>");
	return 0;
}
c_RoomMap::c_RoomMap(){
	m_MapWidth=6;
	m_MapHeight=6;
	m_Map=Array<Array<c_Room* > >(6);
	m_currentRoom=0;
	m_roomNum=8;
	m_mapBorder=bb_graphics_LoadImage(String(L"gui/mapBorder.png",17),1,c_Image::m_DefaultFlags);
	m_MapPSize=25;
	m_MapXOffset=485;
}
c_RoomMap* c_RoomMap::m_new(){
	DBG_ENTER("RoomMap.new")
	c_RoomMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<619>");
	return this;
}
int c_RoomMap::p_Build(){
	DBG_ENTER("RoomMap.Build")
	c_RoomMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<630>");
	int t_RoomCount=0;
	DBG_LOCAL(t_RoomCount,"RoomCount")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<632>");
	for(int t_i=0;t_i<=m_MapWidth-1;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<633>");
		gc_assign(m_Map.At(t_i),Array<c_Room* >(m_MapHeight));
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<637>");
	for(int t_i2=0;t_i2<=m_MapWidth-1;t_i2=t_i2+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i2,"i")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<638>");
		for(int t_j=0;t_j<=m_MapHeight-1;t_j=t_j+1){
			DBG_BLOCK();
			DBG_LOCAL(t_j,"j")
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<639>");
			gc_assign(m_Map.At(t_i2).At(t_j),(new c_Room)->m_new(0,t_i2,t_j));
		}
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<645>");
	int t_xr=int(bb_random_Rnd()*Float(m_MapWidth));
	DBG_LOCAL(t_xr,"xr")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<646>");
	int t_yr=int(bb_random_Rnd()*Float(m_MapHeight));
	DBG_LOCAL(t_yr,"yr")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<647>");
	m_Map.At(t_xr).At(t_yr)->p_UpdateType(1);
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<648>");
	gc_assign(m_currentRoom,m_Map.At(t_xr).At(t_yr));
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<649>");
	m_currentRoom->p_UpdateVisible(true);
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<650>");
	m_currentRoom->p_UpdateCurrent(true);
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<651>");
	m_currentRoom->p_SetLayout(0);
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<652>");
	t_RoomCount+=1;
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<654>");
	if(t_xr>0 && t_xr<m_MapWidth-1){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<655>");
		m_Map.At(t_xr+1).At(t_yr)->p_UpdateNeighbours(1);
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<656>");
		m_Map.At(t_xr-1).At(t_yr)->p_UpdateNeighbours(1);
	}else{
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<657>");
		if(t_xr==0){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<658>");
			m_Map.At(t_xr+1).At(t_yr)->p_UpdateNeighbours(1);
		}else{
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<659>");
			if(t_xr==m_MapWidth-1){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<660>");
				m_Map.At(t_xr-1).At(t_yr)->p_UpdateNeighbours(1);
			}
		}
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<663>");
	if(t_yr>0 && t_yr<m_MapHeight-1){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<664>");
		m_Map.At(t_xr).At(t_yr+1)->p_UpdateNeighbours(1);
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<665>");
		m_Map.At(t_xr).At(t_yr-1)->p_UpdateNeighbours(1);
	}else{
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<666>");
		if(t_yr==0){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<667>");
			m_Map.At(t_xr).At(t_yr+1)->p_UpdateNeighbours(1);
		}else{
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<668>");
			if(t_yr==m_MapHeight-1){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<669>");
				m_Map.At(t_xr).At(t_yr-1)->p_UpdateNeighbours(1);
			}
		}
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<673>");
	while(t_RoomCount<m_roomNum){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<674>");
		c_List4* t_Valid=(new c_List4)->m_new();
		DBG_LOCAL(t_Valid,"Valid")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<676>");
		for(int t_i3=0;t_i3<=m_MapWidth-1;t_i3=t_i3+1){
			DBG_BLOCK();
			DBG_LOCAL(t_i3,"i")
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<677>");
			for(int t_j2=0;t_j2<=m_MapHeight-1;t_j2=t_j2+1){
				DBG_BLOCK();
				DBG_LOCAL(t_j2,"j")
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<678>");
				if(m_Map.At(t_i3).At(t_j2)->p_GetNeighbours()==1 && m_Map.At(t_i3).At(t_j2)->p_GetType()==0){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<679>");
					t_Valid->p_AddLast4(m_Map.At(t_i3).At(t_j2));
				}
			}
		}
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<685>");
		Array<c_Room* > t_validRoomArray=t_Valid->p_ToArray();
		DBG_LOCAL(t_validRoomArray,"validRoomArray")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<688>");
		int t_ir=int(bb_random_Rnd()*Float(t_validRoomArray.Length())-FLOAT(1.0));
		DBG_LOCAL(t_ir,"ir")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<689>");
		t_validRoomArray.At(t_ir)->p_UpdateType(2);
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<690>");
		t_validRoomArray.At(t_ir)->p_SetLayout(1);
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<691>");
		t_RoomCount+=1;
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<694>");
		int t_x=t_validRoomArray.At(t_ir)->p_GetX();
		DBG_LOCAL(t_x,"x")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<695>");
		int t_y=t_validRoomArray.At(t_ir)->p_GetY();
		DBG_LOCAL(t_y,"y")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<698>");
		if(t_x>0 && t_x<m_MapWidth-1){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<699>");
			m_Map.At(t_x+1).At(t_y)->p_UpdateNeighbours(1);
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<700>");
			m_Map.At(t_x-1).At(t_y)->p_UpdateNeighbours(1);
		}else{
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<701>");
			if(t_x==0){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<702>");
				m_Map.At(t_x+1).At(t_y)->p_UpdateNeighbours(1);
			}else{
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<703>");
				if(t_x==m_MapWidth-1){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<704>");
					m_Map.At(t_x-1).At(t_y)->p_UpdateNeighbours(1);
				}
			}
		}
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<707>");
		if(t_y>0 && t_y<m_MapHeight-1){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<708>");
			m_Map.At(t_x).At(t_y+1)->p_UpdateNeighbours(1);
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<709>");
			m_Map.At(t_x).At(t_y-1)->p_UpdateNeighbours(1);
		}else{
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<710>");
			if(t_y==0){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<711>");
				m_Map.At(t_x).At(t_y+1)->p_UpdateNeighbours(1);
			}else{
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<712>");
				if(t_y==m_MapHeight-1){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<713>");
					m_Map.At(t_x).At(t_y-1)->p_UpdateNeighbours(1);
				}
			}
		}
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<719>");
	c_List4* t_EndRooms=0;
	DBG_LOCAL(t_EndRooms,"EndRooms")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<720>");
	t_EndRooms=(new c_List4)->m_new();
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<722>");
	for(int t_i4=0;t_i4<=m_MapWidth-1;t_i4=t_i4+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i4,"i")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<723>");
		for(int t_j3=0;t_j3<=m_MapHeight-1;t_j3=t_j3+1){
			DBG_BLOCK();
			DBG_LOCAL(t_j3,"j")
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<724>");
			if(m_Map.At(t_i4).At(t_j3)->p_GetNeighbours()==1 && m_Map.At(t_i4).At(t_j3)->p_GetType()==2){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<725>");
				t_EndRooms->p_AddLast4(m_Map.At(t_i4).At(t_j3));
			}
		}
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<730>");
	Array<c_Room* > t_EndRoomsArray=t_EndRooms->p_ToArray();
	DBG_LOCAL(t_EndRoomsArray,"EndRoomsArray")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<731>");
	int t_Boss=int(bb_random_Rnd()*Float(t_EndRoomsArray.Length())-FLOAT(1.0));
	DBG_LOCAL(t_Boss,"Boss")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<733>");
	t_EndRoomsArray.At(t_Boss)->p_UpdateType(3);
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<734>");
	t_EndRoomsArray.At(t_Boss)->p_SetLayout(3);
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<738>");
	for(int t_x2=0;t_x2<=m_MapWidth-1;t_x2=t_x2+1){
		DBG_BLOCK();
		DBG_LOCAL(t_x2,"x")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<739>");
		for(int t_y2=0;t_y2<=m_MapHeight-1;t_y2=t_y2+1){
			DBG_BLOCK();
			DBG_LOCAL(t_y2,"y")
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<740>");
			if(m_Map.At(t_x2).At(t_y2)->p_GetType()!=0){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<741>");
				if(t_y2>0){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<742>");
					if(m_Map.At(t_x2).At(t_y2-1)->p_GetType()!=0 && t_y2>0){
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<743>");
						m_Map.At(t_x2).At(t_y2)->p_SetnDoor(String(L"1",1));
					}
				}
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<746>");
				if(t_y2<m_MapHeight-1){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<747>");
					if(m_Map.At(t_x2).At(t_y2+1)->p_GetType()!=0 && t_y2<m_MapHeight-1){
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<748>");
						m_Map.At(t_x2).At(t_y2)->p_SetsDoor(String(L"1",1));
					}
				}
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<751>");
				if(t_x2>0){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<752>");
					if(m_Map.At(t_x2-1).At(t_y2)->p_GetType()!=0 && t_x2>0){
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<753>");
						m_Map.At(t_x2).At(t_y2)->p_SetwDoor(String(L"1",1));
					}
				}
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<756>");
				if(t_x2<m_MapWidth-1){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<757>");
					if(m_Map.At(t_x2+1).At(t_y2)->p_GetType()!=0 && t_x2<m_MapWidth-1){
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<758>");
						m_Map.At(t_x2).At(t_y2)->p_SeteDoor(String(L"1",1));
					}
				}
			}
		}
	}
	return 0;
}
int c_RoomMap::p_UpdateCurrentRoom(c_Player* t_player){
	DBG_ENTER("RoomMap.UpdateCurrentRoom")
	c_RoomMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_player,"player")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<788>");
	m_currentRoom->p_UpdateRoom(t_player);
	return 0;
}
bool c_RoomMap::p_TileCollision(Float t_x,Float t_y){
	DBG_ENTER("RoomMap.TileCollision")
	c_RoomMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<796>");
	bool t_=m_currentRoom->p_TileCollision(t_x,t_y);
	return t_;
}
bool c_RoomMap::p_DamageCollision(Float t_x,Float t_y){
	DBG_ENTER("RoomMap.DamageCollision")
	c_RoomMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<800>");
	bool t_=m_currentRoom->p_DamageCollision(t_x,t_y);
	return t_;
}
int c_RoomMap::p_MoveRoom(String t_direction){
	DBG_ENTER("RoomMap.MoveRoom")
	c_RoomMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_direction,"direction")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<804>");
	String t_3=t_direction;
	DBG_LOCAL(t_3,"3")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<805>");
	if(t_3==String(L"NORTH",5)){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<806>");
		m_currentRoom->p_UpdateCurrent(false);
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<807>");
		gc_assign(m_currentRoom,m_Map.At(m_currentRoom->p_GetX()).At(m_currentRoom->p_GetY()-1));
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<808>");
		m_currentRoom->p_UpdateVisible(true);
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<809>");
		m_currentRoom->p_UpdateCurrent(true);
	}else{
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<810>");
		if(t_3==String(L"SOUTH",5)){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<811>");
			m_currentRoom->p_UpdateCurrent(false);
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<812>");
			gc_assign(m_currentRoom,m_Map.At(m_currentRoom->p_GetX()).At(m_currentRoom->p_GetY()+1));
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<813>");
			m_currentRoom->p_UpdateVisible(true);
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<814>");
			m_currentRoom->p_UpdateCurrent(true);
		}else{
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<815>");
			if(t_3==String(L"EAST",4)){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<816>");
				m_currentRoom->p_UpdateCurrent(false);
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<817>");
				gc_assign(m_currentRoom,m_Map.At(m_currentRoom->p_GetX()-1).At(m_currentRoom->p_GetY()));
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<818>");
				m_currentRoom->p_UpdateVisible(true);
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<819>");
				m_currentRoom->p_UpdateCurrent(true);
			}else{
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<820>");
				if(t_3==String(L"WEST",4)){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<821>");
					m_currentRoom->p_UpdateCurrent(false);
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<822>");
					gc_assign(m_currentRoom,m_Map.At(m_currentRoom->p_GetX()+1).At(m_currentRoom->p_GetY()));
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<823>");
					m_currentRoom->p_UpdateVisible(true);
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<824>");
					m_currentRoom->p_UpdateCurrent(true);
				}
			}
		}
	}
	return 0;
}
int c_RoomMap::p_InteractCurrentRoom(c_Player* t_player){
	DBG_ENTER("RoomMap.InteractCurrentRoom")
	c_RoomMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_player,"player")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<784>");
	m_currentRoom->p_Interact(t_player);
	return 0;
}
c_List3* c_RoomMap::p_GetCurrentRoomEnemies(){
	DBG_ENTER("RoomMap.GetCurrentRoomEnemies")
	c_RoomMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<792>");
	c_List3* t_=m_currentRoom->p_GetEnemyList();
	return t_;
}
int c_RoomMap::p_Reset(){
	DBG_ENTER("RoomMap.Reset")
	c_RoomMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<867>");
	for(int t_x=0;t_x<=m_MapWidth-1;t_x=t_x+1){
		DBG_BLOCK();
		DBG_LOCAL(t_x,"x")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<868>");
		for(int t_y=0;t_y<=m_MapHeight-1;t_y=t_y+1){
			DBG_BLOCK();
			DBG_LOCAL(t_y,"y")
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<869>");
			m_Map.At(t_x).At(t_y)->p_Reset();
		}
	}
	return 0;
}
int c_RoomMap::p_DrawRoomFloor(){
	DBG_ENTER("RoomMap.DrawRoomFloor")
	c_RoomMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<774>");
	m_currentRoom->p_DrawFloor();
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<775>");
	m_currentRoom->p_DrawEnemies();
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<776>");
	m_currentRoom->p_DrawItems();
	return 0;
}
int c_RoomMap::p_DrawRoomWalls(){
	DBG_ENTER("RoomMap.DrawRoomWalls")
	c_RoomMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<780>");
	m_currentRoom->p_DrawWalls();
	return 0;
}
int c_RoomMap::p_DrawMap(){
	DBG_ENTER("RoomMap.DrawMap")
	c_RoomMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<831>");
	bb_graphics_DrawImage(m_mapBorder,FLOAT(480.0),FLOAT(0.0),0);
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<832>");
	for(int t_x=0;t_x<=m_MapWidth-1;t_x=t_x+1){
		DBG_BLOCK();
		DBG_LOCAL(t_x,"x")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<833>");
		for(int t_y=0;t_y<=m_MapHeight-1;t_y=t_y+1){
			DBG_BLOCK();
			DBG_LOCAL(t_y,"y")
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<834>");
			if(m_Map.At(t_x).At(t_y)->p_GetVisible()==true){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<835>");
				int t_4=m_Map.At(t_x).At(t_y)->p_GetType();
				DBG_LOCAL(t_4,"4")
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<836>");
				if(t_4==0){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<837>");
					bb_graphics_SetColor(FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<838>");
					bb_graphics_DrawRect(Float(t_x*m_MapPSize+m_MapXOffset),Float(t_y*m_MapPSize),Float(m_MapPSize),Float(m_MapPSize));
				}else{
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<839>");
					if(t_4==1){
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<840>");
						if(m_Map.At(t_x).At(t_y)->p_GetCurrent()==true){
							DBG_BLOCK();
							DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<841>");
							bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
							DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<842>");
							bb_graphics_DrawRect(Float(t_x*m_MapPSize+m_MapXOffset),Float(t_y*m_MapPSize),Float(m_MapPSize),Float(m_MapPSize));
						}
						DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<844>");
						bb_LevelGeneration_DrawRoom(String(L"levels/StartRoom/",17)+m_Map.At(t_x).At(t_y)->p_GetDoors()+String(L".png",4),t_x,t_y,m_MapPSize,m_MapXOffset);
					}else{
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<845>");
						if(t_4==2){
							DBG_BLOCK();
							DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<846>");
							if(m_Map.At(t_x).At(t_y)->p_GetCurrent()==true){
								DBG_BLOCK();
								DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<847>");
								bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
								DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<848>");
								bb_graphics_DrawRect(Float(t_x*m_MapPSize+m_MapXOffset),Float(t_y*m_MapPSize),Float(m_MapPSize),Float(m_MapPSize));
							}
							DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<850>");
							bb_LevelGeneration_DrawRoom(String(L"levels/Room/",12)+m_Map.At(t_x).At(t_y)->p_GetDoors()+String(L".png",4),t_x,t_y,m_MapPSize,m_MapXOffset);
						}else{
							DBG_BLOCK();
							DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<851>");
							if(t_4==3){
								DBG_BLOCK();
								DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<852>");
								if(m_Map.At(t_x).At(t_y)->p_GetCurrent()==true){
									DBG_BLOCK();
									DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<853>");
									bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
									DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<854>");
									bb_graphics_DrawRect(Float(t_x*m_MapPSize+m_MapXOffset),Float(t_y*m_MapPSize),Float(m_MapPSize),Float(m_MapPSize));
								}
								DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<856>");
								bb_LevelGeneration_DrawRoom(String(L"levels/BossRoom/",16)+m_Map.At(t_x).At(t_y)->p_GetDoors()+String(L".png",4),t_x,t_y,m_MapPSize,m_MapXOffset);
							}
						}
					}
				}
			}
		}
	}
	return 0;
}
void c_RoomMap::mark(){
	Object::mark();
	gc_mark_q(m_Map);
	gc_mark_q(m_currentRoom);
	gc_mark_q(m_mapBorder);
}
String c_RoomMap::debug(){
	String t="(RoomMap)\n";
	t+=dbg_decl("Map",&m_Map);
	t+=dbg_decl("MapHeight",&m_MapHeight);
	t+=dbg_decl("MapWidth",&m_MapWidth);
	t+=dbg_decl("MapPSize",&m_MapPSize);
	t+=dbg_decl("MapXOffset",&m_MapXOffset);
	t+=dbg_decl("roomNum",&m_roomNum);
	t+=dbg_decl("currentRoom",&m_currentRoom);
	t+=dbg_decl("mapBorder",&m_mapBorder);
	return t;
}
int bb_input_KeyHit(int t_key){
	DBG_ENTER("KeyHit")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/input.monkey<44>");
	int t_=bb_input_device->p_KeyHit(t_key);
	return t_;
}
int bb_audio_StopMusic(){
	DBG_ENTER("StopMusic")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/audio.monkey<89>");
	bb_audio_device->StopMusic();
	return 0;
}
int bb_app_Millisecs(){
	DBG_ENTER("Millisecs")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<233>");
	int t_=bb_app__game->Millisecs();
	return t_;
}
int bb_random_Seed;
c_Room::c_Room(){
	m_x=0;
	m_y=0;
	m_Type=0;
	m_droppedItems=0;
	m_droppedWeapons=0;
	m_enemyList=0;
	m_WallLayout=Array<Array<int > >();
	m_FloorLayout=Array<Array<int > >();
	m_CollisionArray=Array<Array<int > >();
	m_visible=false;
	m_current=false;
	m_enemyCount=0;
	m_Neighbours=0;
	m_nDoor=String(L"0",1);
	m_sDoor=String(L"0",1);
	m_wDoor=String(L"0",1);
	m_eDoor=String(L"0",1);
	m_complete=false;
	m_RoomSize32=15;
	m_RoomTiles=bb_graphics_LoadImage(String(L"levels/ProtoTileSet.png",23),1,c_Image::m_DefaultFlags);
	m_DoorArray=Array<String >();
}
c_Room* c_Room::m_new(int t_Type,int t_X,int t_Y){
	DBG_ENTER("Room.new")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Type,"Type")
	DBG_LOCAL(t_X,"X")
	DBG_LOCAL(t_Y,"Y")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<32>");
	this->m_x=t_X;
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<33>");
	this->m_y=t_Y;
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<34>");
	this->m_Type=t_Type;
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<35>");
	gc_assign(m_droppedItems,(new c_List)->m_new());
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<36>");
	gc_assign(m_droppedWeapons,(new c_List2)->m_new());
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<37>");
	gc_assign(m_enemyList,(new c_List3)->m_new());
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<52>");
	int t_[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,2};
	int t_2[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_3[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_4[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_5[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_6[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_7[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_8[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_9[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_10[]={3,8,8,12,8,8,8,8,8,8,8,8,8,8,4};
	int t_11[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_12[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_13[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_14[]={3,7,7,7,7,7,7,7,7,7,7,7,7,7,4};
	int t_15[]={5,10,10,10,10,10,10,10,10,10,10,10,10,10,6};
	Array<int > t_16[]={Array<int >(t_,15),Array<int >(t_2,15),Array<int >(t_3,15),Array<int >(t_4,15),Array<int >(t_5,15),Array<int >(t_6,15),Array<int >(t_7,15),Array<int >(t_8,15),Array<int >(t_9,15),Array<int >(t_10,15),Array<int >(t_11,15),Array<int >(t_12,15),Array<int >(t_13,15),Array<int >(t_14,15),Array<int >(t_15,15)};
	gc_assign(m_WallLayout,Array<Array<int > >(t_16,15));
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<67>");
	int t_17[]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	int t_18[]={0,3,3,3,3,3,3,3,3,3,3,3,3,3,0};
	int t_19[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
	int t_20[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
	int t_21[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
	int t_22[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
	int t_23[]={0,1,1,1,1,1,2,2,2,1,1,1,1,1,0};
	int t_24[]={0,1,1,1,1,1,2,2,2,1,1,1,1,1,0};
	int t_25[]={0,1,1,1,1,1,2,2,2,1,1,1,1,1,0};
	int t_26[]={0,1,1,1,1,1,2,2,2,1,1,1,1,1,0};
	int t_27[]={0,1,1,11,1,1,1,1,1,1,1,1,1,1,0};
	int t_28[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
	int t_29[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
	int t_30[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
	int t_31[]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	Array<int > t_32[]={Array<int >(t_17,15),Array<int >(t_18,15),Array<int >(t_19,15),Array<int >(t_20,15),Array<int >(t_21,15),Array<int >(t_22,15),Array<int >(t_23,15),Array<int >(t_24,15),Array<int >(t_25,15),Array<int >(t_26,15),Array<int >(t_27,15),Array<int >(t_28,15),Array<int >(t_29,15),Array<int >(t_30,15),Array<int >(t_31,15)};
	gc_assign(m_FloorLayout,Array<Array<int > >(t_32,15));
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<82>");
	int t_33[]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
	int t_34[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_35[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_36[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_37[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_38[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_39[]={1,0,0,0,0,0,2,2,2,0,0,0,0,0,1};
	int t_40[]={1,0,0,0,0,0,2,2,2,0,0,0,0,0,1};
	int t_41[]={1,0,0,0,0,0,2,2,2,0,0,0,0,0,1};
	int t_42[]={1,0,0,0,0,0,2,2,2,0,0,0,0,0,1};
	int t_43[]={1,0,0,1,0,0,0,0,0,0,0,0,0,0,1};
	int t_44[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_45[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_46[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_47[]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
	Array<int > t_48[]={Array<int >(t_33,15),Array<int >(t_34,15),Array<int >(t_35,15),Array<int >(t_36,15),Array<int >(t_37,15),Array<int >(t_38,15),Array<int >(t_39,15),Array<int >(t_40,15),Array<int >(t_41,15),Array<int >(t_42,15),Array<int >(t_43,15),Array<int >(t_44,15),Array<int >(t_45,15),Array<int >(t_46,15),Array<int >(t_47,15)};
	gc_assign(m_CollisionArray,Array<Array<int > >(t_48,15));
	return this;
}
c_Room* c_Room::m_new2(){
	DBG_ENTER("Room.new")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<7>");
	return this;
}
int c_Room::p_UpdateType(int t_Type){
	DBG_ENTER("Room.UpdateType")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Type,"Type")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<485>");
	this->m_Type=t_Type;
	return 0;
}
int c_Room::p_UpdateVisible(bool t__visible){
	DBG_ENTER("Room.UpdateVisible")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__visible,"_visible")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<493>");
	m_visible=t__visible;
	return 0;
}
int c_Room::p_UpdateCurrent(bool t__current){
	DBG_ENTER("Room.UpdateCurrent")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__current,"_current")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<489>");
	m_current=t__current;
	return 0;
}
int c_Room::p_SetLayout(int t__layout){
	DBG_ENTER("Room.SetLayout")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__layout,"_layout")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<283>");
	int t_2=t__layout;
	DBG_LOCAL(t_2,"2")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<284>");
	if(t_2==0){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<285>");
		m_enemyList->p_Clear();
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<286>");
		c_Sword* t_largeSword=(new c_Sword)->m_new(10,String(L"items/largeSword.png",20),30,12,1);
		DBG_LOCAL(t_largeSword,"largeSword")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<287>");
		m_droppedWeapons->p_AddLast2(t_largeSword);
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<302>");
		int t_[]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
		int t_3[]={0,3,3,3,3,3,3,3,3,3,3,3,3,3,0};
		int t_4[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
		int t_5[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
		int t_6[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
		int t_7[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
		int t_8[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
		int t_9[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
		int t_10[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
		int t_11[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
		int t_12[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
		int t_13[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
		int t_14[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
		int t_15[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
		int t_16[]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
		Array<int > t_17[]={Array<int >(t_,15),Array<int >(t_3,15),Array<int >(t_4,15),Array<int >(t_5,15),Array<int >(t_6,15),Array<int >(t_7,15),Array<int >(t_8,15),Array<int >(t_9,15),Array<int >(t_10,15),Array<int >(t_11,15),Array<int >(t_12,15),Array<int >(t_13,15),Array<int >(t_14,15),Array<int >(t_15,15),Array<int >(t_16,15)};
		gc_assign(m_FloorLayout,Array<Array<int > >(t_17,15));
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<317>");
		int t_18[]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
		int t_19[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
		int t_20[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
		int t_21[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
		int t_22[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
		int t_23[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
		int t_24[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
		int t_25[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
		int t_26[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
		int t_27[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
		int t_28[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
		int t_29[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
		int t_30[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
		int t_31[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
		int t_32[]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
		Array<int > t_33[]={Array<int >(t_18,15),Array<int >(t_19,15),Array<int >(t_20,15),Array<int >(t_21,15),Array<int >(t_22,15),Array<int >(t_23,15),Array<int >(t_24,15),Array<int >(t_25,15),Array<int >(t_26,15),Array<int >(t_27,15),Array<int >(t_28,15),Array<int >(t_29,15),Array<int >(t_30,15),Array<int >(t_31,15),Array<int >(t_32,15)};
		gc_assign(m_CollisionArray,Array<Array<int > >(t_33,15));
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<332>");
		int t_34[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,2};
		int t_35[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
		int t_36[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
		int t_37[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
		int t_38[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
		int t_39[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
		int t_40[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
		int t_41[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
		int t_42[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
		int t_43[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
		int t_44[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
		int t_45[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
		int t_46[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
		int t_47[]={3,7,7,7,7,7,7,7,7,7,7,7,7,7,4};
		int t_48[]={5,10,10,10,10,10,10,10,10,10,10,10,10,10,6};
		Array<int > t_49[]={Array<int >(t_34,15),Array<int >(t_35,15),Array<int >(t_36,15),Array<int >(t_37,15),Array<int >(t_38,15),Array<int >(t_39,15),Array<int >(t_40,15),Array<int >(t_41,15),Array<int >(t_42,15),Array<int >(t_43,15),Array<int >(t_44,15),Array<int >(t_45,15),Array<int >(t_46,15),Array<int >(t_47,15),Array<int >(t_48,15)};
		gc_assign(m_WallLayout,Array<Array<int > >(t_49,15));
	}else{
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<333>");
		if(t_2==1){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<348>");
			int t_50[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,2};
			int t_51[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
			int t_52[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
			int t_53[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
			int t_54[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
			int t_55[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
			int t_56[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
			int t_57[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
			int t_58[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
			int t_59[]={3,8,8,12,8,8,8,8,8,8,8,8,8,8,4};
			int t_60[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
			int t_61[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
			int t_62[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
			int t_63[]={3,7,7,7,7,7,7,7,7,7,7,7,7,7,4};
			int t_64[]={5,10,10,10,10,10,10,10,10,10,10,10,10,10,6};
			Array<int > t_65[]={Array<int >(t_50,15),Array<int >(t_51,15),Array<int >(t_52,15),Array<int >(t_53,15),Array<int >(t_54,15),Array<int >(t_55,15),Array<int >(t_56,15),Array<int >(t_57,15),Array<int >(t_58,15),Array<int >(t_59,15),Array<int >(t_60,15),Array<int >(t_61,15),Array<int >(t_62,15),Array<int >(t_63,15),Array<int >(t_64,15)};
			gc_assign(m_WallLayout,Array<Array<int > >(t_65,15));
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<363>");
			int t_66[]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
			int t_67[]={0,3,3,3,3,3,3,3,3,3,3,3,3,3,0};
			int t_68[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
			int t_69[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
			int t_70[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
			int t_71[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
			int t_72[]={0,1,1,1,1,1,2,2,2,1,1,1,1,1,0};
			int t_73[]={0,1,1,1,1,1,2,2,2,1,1,1,1,1,0};
			int t_74[]={0,1,1,1,1,1,2,2,2,1,1,1,1,1,0};
			int t_75[]={0,1,1,1,1,1,2,2,2,1,1,1,1,1,0};
			int t_76[]={0,1,1,11,1,1,1,1,1,1,1,1,1,1,0};
			int t_77[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
			int t_78[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
			int t_79[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
			int t_80[]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
			Array<int > t_81[]={Array<int >(t_66,15),Array<int >(t_67,15),Array<int >(t_68,15),Array<int >(t_69,15),Array<int >(t_70,15),Array<int >(t_71,15),Array<int >(t_72,15),Array<int >(t_73,15),Array<int >(t_74,15),Array<int >(t_75,15),Array<int >(t_76,15),Array<int >(t_77,15),Array<int >(t_78,15),Array<int >(t_79,15),Array<int >(t_80,15)};
			gc_assign(m_FloorLayout,Array<Array<int > >(t_81,15));
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<378>");
			int t_82[]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
			int t_83[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
			int t_84[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
			int t_85[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
			int t_86[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
			int t_87[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
			int t_88[]={1,0,0,0,0,0,2,2,2,0,0,0,0,0,1};
			int t_89[]={1,0,0,0,0,0,2,2,2,0,0,0,0,0,1};
			int t_90[]={1,0,0,0,0,0,2,2,2,0,0,0,0,0,1};
			int t_91[]={1,0,0,0,0,0,2,2,2,0,0,0,0,0,1};
			int t_92[]={1,0,0,1,0,0,0,0,0,0,0,0,0,0,1};
			int t_93[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
			int t_94[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
			int t_95[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
			int t_96[]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
			Array<int > t_97[]={Array<int >(t_82,15),Array<int >(t_83,15),Array<int >(t_84,15),Array<int >(t_85,15),Array<int >(t_86,15),Array<int >(t_87,15),Array<int >(t_88,15),Array<int >(t_89,15),Array<int >(t_90,15),Array<int >(t_91,15),Array<int >(t_92,15),Array<int >(t_93,15),Array<int >(t_94,15),Array<int >(t_95,15),Array<int >(t_96,15)};
			gc_assign(m_CollisionArray,Array<Array<int > >(t_97,15));
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<379>");
			m_enemyList->p_Clear();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<380>");
			m_enemyCount=3;
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<381>");
			m_enemyList->p_AddLast3((new c_Enemy)->m_new(1,FLOAT(250.0),FLOAT(360.0),false));
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<382>");
			m_enemyList->p_AddLast3((new c_Enemy)->m_new(2,FLOAT(100.0),FLOAT(200.0),false));
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<383>");
			m_enemyList->p_AddLast3((new c_Enemy)->m_new(1,FLOAT(350.0),FLOAT(350.0),false));
		}else{
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<384>");
			if(t_2==3){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<399>");
				int t_98[]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
				int t_99[]={0,3,3,3,3,3,3,3,3,3,3,3,3,3,0};
				int t_100[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
				int t_101[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
				int t_102[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
				int t_103[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
				int t_104[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
				int t_105[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
				int t_106[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
				int t_107[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
				int t_108[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
				int t_109[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
				int t_110[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
				int t_111[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,0};
				int t_112[]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
				Array<int > t_113[]={Array<int >(t_98,15),Array<int >(t_99,15),Array<int >(t_100,15),Array<int >(t_101,15),Array<int >(t_102,15),Array<int >(t_103,15),Array<int >(t_104,15),Array<int >(t_105,15),Array<int >(t_106,15),Array<int >(t_107,15),Array<int >(t_108,15),Array<int >(t_109,15),Array<int >(t_110,15),Array<int >(t_111,15),Array<int >(t_112,15)};
				gc_assign(m_FloorLayout,Array<Array<int > >(t_113,15));
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<414>");
				int t_114[]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
				int t_115[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
				int t_116[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
				int t_117[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
				int t_118[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
				int t_119[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
				int t_120[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
				int t_121[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
				int t_122[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
				int t_123[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
				int t_124[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
				int t_125[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
				int t_126[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
				int t_127[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
				int t_128[]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
				Array<int > t_129[]={Array<int >(t_114,15),Array<int >(t_115,15),Array<int >(t_116,15),Array<int >(t_117,15),Array<int >(t_118,15),Array<int >(t_119,15),Array<int >(t_120,15),Array<int >(t_121,15),Array<int >(t_122,15),Array<int >(t_123,15),Array<int >(t_124,15),Array<int >(t_125,15),Array<int >(t_126,15),Array<int >(t_127,15),Array<int >(t_128,15)};
				gc_assign(m_CollisionArray,Array<Array<int > >(t_129,15));
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<429>");
				int t_130[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,2};
				int t_131[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
				int t_132[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
				int t_133[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
				int t_134[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
				int t_135[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
				int t_136[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
				int t_137[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
				int t_138[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
				int t_139[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
				int t_140[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
				int t_141[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
				int t_142[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
				int t_143[]={3,7,7,7,7,7,7,7,7,7,7,7,7,7,4};
				int t_144[]={5,10,10,10,10,10,10,10,10,10,10,10,10,10,6};
				Array<int > t_145[]={Array<int >(t_130,15),Array<int >(t_131,15),Array<int >(t_132,15),Array<int >(t_133,15),Array<int >(t_134,15),Array<int >(t_135,15),Array<int >(t_136,15),Array<int >(t_137,15),Array<int >(t_138,15),Array<int >(t_139,15),Array<int >(t_140,15),Array<int >(t_141,15),Array<int >(t_142,15),Array<int >(t_143,15),Array<int >(t_144,15)};
				gc_assign(m_WallLayout,Array<Array<int > >(t_145,15));
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<430>");
				m_enemyList->p_Clear();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<431>");
				m_enemyCount=1;
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<432>");
				m_enemyList->p_AddLast3((new c_Enemy)->m_new(10,FLOAT(100.0),FLOAT(250.0),false));
			}
		}
	}
	return 0;
}
int c_Room::p_UpdateNeighbours(int t_Amount){
	DBG_ENTER("Room.UpdateNeighbours")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Amount,"Amount")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<481>");
	this->m_Neighbours+=t_Amount;
	return 0;
}
int c_Room::p_GetNeighbours(){
	DBG_ENTER("Room.GetNeighbours")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<497>");
	return m_Neighbours;
}
int c_Room::p_GetType(){
	DBG_ENTER("Room.GetType")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<501>");
	return m_Type;
}
int c_Room::p_GetX(){
	DBG_ENTER("Room.GetX")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<516>");
	return this->m_x;
}
int c_Room::p_GetY(){
	DBG_ENTER("Room.GetY")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<520>");
	return this->m_y;
}
int c_Room::p_SetnDoor(String t__nDoor){
	DBG_ENTER("Room.SetnDoor")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__nDoor,"_nDoor")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<436>");
	this->m_nDoor=t__nDoor;
	return 0;
}
int c_Room::p_SetsDoor(String t__sDoor){
	DBG_ENTER("Room.SetsDoor")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__sDoor,"_sDoor")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<440>");
	this->m_sDoor=t__sDoor;
	return 0;
}
int c_Room::p_SetwDoor(String t__wDoor){
	DBG_ENTER("Room.SetwDoor")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__wDoor,"_wDoor")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<448>");
	this->m_wDoor=t__wDoor;
	return 0;
}
int c_Room::p_SeteDoor(String t__eDoor){
	DBG_ENTER("Room.SeteDoor")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__eDoor,"_eDoor")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<444>");
	this->m_eDoor=t__eDoor;
	return 0;
}
int c_Room::p_UpdateRoom(c_Player* t_player){
	DBG_ENTER("Room.UpdateRoom")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_player,"player")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<168>");
	c_Enumerator2* t_=m_enemyList->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Enemy* t_i=t_->p_NextObject();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<169>");
		t_i->p_CheckActive(t_player->p_GetCentreX(),t_player->p_GetCentreY());
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<170>");
		t_i->p_CalcMove(t_player->p_GetCentreX(),t_player->p_GetCentreY());
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<171>");
		t_i->p_MoveX();
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<172>");
		if(bb_Enemies_CheckCollision(t_i,m_enemyList)==true){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<173>");
			t_i->p_CorrectX();
		}
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<175>");
		if(bb_BasicLibrary_Collide(int(t_player->p_GetX()),int(t_player->p_GetY()),t_player->p_GetWidth(),t_player->p_GetHeight(),t_i->p_GetX(),t_i->p_GetY(),t_i->p_GetWidth(),t_i->p_GetHeight())==true){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<176>");
			t_i->p_CorrectX();
		}
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<178>");
		t_i->p_MoveY();
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<179>");
		if(bb_Enemies_CheckCollision(t_i,m_enemyList)==true){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<180>");
			t_i->p_CorrectY();
		}
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<182>");
		if(bb_BasicLibrary_Collide(int(t_player->p_GetX()),int(t_player->p_GetY()),t_player->p_GetWidth(),t_player->p_GetHeight(),t_i->p_GetX(),t_i->p_GetY(),t_i->p_GetWidth(),t_i->p_GetHeight())==true){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<183>");
			t_i->p_CorrectY();
		}
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<185>");
		if(t_i->p_GetType()!=2){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<186>");
			t_i->p_Attack3(t_player);
		}else{
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<188>");
			if(t_i->p_Summon(t_player)==true && m_enemyCount<10){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<189>");
				m_enemyList->p_AddLast3((new c_Enemy)->m_new(4,Float(t_i->p_GetX()-30),Float(t_i->p_GetY()),true));
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<190>");
				m_enemyList->p_AddLast3((new c_Enemy)->m_new(4,Float(t_i->p_GetX()+30),Float(t_i->p_GetY()),true));
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<191>");
				m_enemyList->p_AddLast3((new c_Enemy)->m_new(4,Float(t_i->p_GetX()),Float(t_i->p_GetY()+30),true));
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<192>");
				m_enemyCount+=3;
			}
		}
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<196>");
		if(t_i->p_CheckHealth()==0){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<197>");
			if(t_player->p_GetCurHealth()<3 && t_i->p_GetType()!=4){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<198>");
				Float t_chance=bb_random_Rnd();
				DBG_LOCAL(t_chance,"chance")
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<199>");
				if(t_chance>FLOAT(0.6)){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<200>");
					c_HealthPotion* t_hp=(new c_HealthPotion)->m_new(Float(t_i->p_GetX()),Float(t_i->p_GetY()));
					DBG_LOCAL(t_hp,"hp")
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<201>");
					m_droppedItems->p_AddLast(t_hp);
				}
			}
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<204>");
			m_enemyList->p_Remove2(t_i);
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<205>");
			m_enemyCount-=1;
		}
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<209>");
	if(m_enemyCount==0){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<210>");
		m_complete=true;
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<213>");
	if(m_complete==true){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<214>");
		if(m_wDoor==String(L"1",1)){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<215>");
			m_CollisionArray.At(7).At(0)=0;
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<216>");
			m_WallLayout.At(7).At(0)=8;
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<217>");
			m_FloorLayout.At(7).At(0)=1;
		}
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<219>");
		if(m_eDoor==String(L"1",1)){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<220>");
			m_CollisionArray.At(7).At(14)=0;
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<221>");
			m_WallLayout.At(7).At(14)=8;
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<222>");
			m_FloorLayout.At(7).At(14)=1;
		}
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<224>");
		if(m_nDoor==String(L"1",1)){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<225>");
			m_CollisionArray.At(0).At(7)=0;
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<226>");
			m_CollisionArray.At(1).At(7)=0;
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<227>");
			m_WallLayout.At(0).At(7)=8;
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<228>");
			m_WallLayout.At(1).At(7)=8;
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<229>");
			m_FloorLayout.At(0).At(7)=1;
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<230>");
			m_FloorLayout.At(1).At(7)=1;
		}
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<232>");
		if(m_sDoor==String(L"1",1)){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<233>");
			m_CollisionArray.At(14).At(7)=0;
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<234>");
			m_CollisionArray.At(13).At(7)=0;
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<235>");
			m_WallLayout.At(13).At(7)=8;
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<236>");
			m_WallLayout.At(14).At(7)=8;
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<237>");
			m_FloorLayout.At(14).At(7)=1;
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<238>");
			m_FloorLayout.At(13).At(7)=1;
		}
	}
	return 0;
}
bool c_Room::p_TileCollision(Float t_x1,Float t_y1){
	DBG_ENTER("Room.TileCollision")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x1,"x1")
	DBG_LOCAL(t_y1,"y1")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<578>");
	int t_tileLeft=int(t_x1/FLOAT(32.0));
	DBG_LOCAL(t_tileLeft,"tileLeft")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<579>");
	int t_tileRight=int((t_x1+FLOAT(19.0))/FLOAT(32.0));
	DBG_LOCAL(t_tileRight,"tileRight")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<580>");
	int t_tileTop=int(t_y1/FLOAT(32.0));
	DBG_LOCAL(t_tileTop,"tileTop")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<581>");
	int t_tileBot=int((t_y1+FLOAT(29.0))/FLOAT(32.0));
	DBG_LOCAL(t_tileBot,"tileBot")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<583>");
	if(t_tileLeft<0){
		DBG_BLOCK();
		t_tileLeft=0;
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<584>");
	if(t_tileRight>15){
		DBG_BLOCK();
		t_tileRight=15;
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<585>");
	if(t_tileTop<0){
		DBG_BLOCK();
		t_tileTop=0;
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<586>");
	if(t_tileBot>15){
		DBG_BLOCK();
		t_tileBot=15;
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<588>");
	for(int t_i=t_tileLeft;t_i<=t_tileRight;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<589>");
		for(int t_j=t_tileTop;t_j<=t_tileBot;t_j=t_j+1){
			DBG_BLOCK();
			DBG_LOCAL(t_j,"j")
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<590>");
			if(m_CollisionArray.At(t_j).At(t_i)==1){
				DBG_BLOCK();
				return true;
			}
		}
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<593>");
	return false;
}
bool c_Room::p_DamageCollision(Float t_x1,Float t_y1){
	DBG_ENTER("Room.DamageCollision")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x1,"x1")
	DBG_LOCAL(t_y1,"y1")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<597>");
	int t_tileLeft=int(t_x1/FLOAT(32.0));
	DBG_LOCAL(t_tileLeft,"tileLeft")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<598>");
	int t_tileRight=int((t_x1+FLOAT(19.0))/FLOAT(32.0));
	DBG_LOCAL(t_tileRight,"tileRight")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<599>");
	int t_tileTop=int(t_y1/FLOAT(32.0));
	DBG_LOCAL(t_tileTop,"tileTop")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<600>");
	int t_tileBot=int((t_y1+FLOAT(29.0))/FLOAT(32.0));
	DBG_LOCAL(t_tileBot,"tileBot")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<602>");
	if(t_tileLeft<0){
		DBG_BLOCK();
		t_tileLeft=0;
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<603>");
	if(t_tileRight>15){
		DBG_BLOCK();
		t_tileRight=15;
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<604>");
	if(t_tileTop<0){
		DBG_BLOCK();
		t_tileTop=0;
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<605>");
	if(t_tileBot>15){
		DBG_BLOCK();
		t_tileBot=15;
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<607>");
	for(int t_i=t_tileLeft;t_i<=t_tileRight;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<608>");
		for(int t_j=t_tileTop;t_j<=t_tileBot;t_j=t_j+1){
			DBG_BLOCK();
			DBG_LOCAL(t_j,"j")
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<609>");
			if(m_CollisionArray.At(t_j).At(t_i)==2){
				DBG_BLOCK();
				return true;
			}
		}
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<612>");
	return false;
}
int c_Room::p_Interact(c_Player* t_player){
	DBG_ENTER("Room.Interact")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_player,"player")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<244>");
	c_Interactables* t_closestItem=0;
	DBG_LOCAL(t_closestItem,"closestItem")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<245>");
	int t_closestDistItem=1000;
	DBG_LOCAL(t_closestDistItem,"closestDistItem")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<246>");
	c_WeaponInterface* t_closestWeapon=0;
	DBG_LOCAL(t_closestWeapon,"closestWeapon")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<247>");
	int t_closestDistWeapon=1000;
	DBG_LOCAL(t_closestDistWeapon,"closestDistWeapon")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<249>");
	c_Enumerator3* t_=m_droppedItems->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Interactables* t_i=t_->p_NextObject();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<250>");
		if(bb_BasicLibrary_findDist(t_player->p_GetCentreX(),t_player->p_GetCentreY(),t_i->p_GetX(),t_i->p_GetY())<t_closestDistItem){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<251>");
			t_closestItem=t_i;
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<252>");
			t_closestDistItem=bb_BasicLibrary_findDist(t_player->p_GetX(),t_player->p_GetY(),t_i->p_GetX(),t_i->p_GetY());
		}
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<256>");
	c_Enumerator4* t_2=m_droppedWeapons->p_ObjectEnumerator();
	while(t_2->p_HasNext()){
		DBG_BLOCK();
		c_WeaponInterface* t_i2=t_2->p_NextObject();
		DBG_LOCAL(t_i2,"i")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<257>");
		if(bb_BasicLibrary_findDist(t_player->p_GetCentreX(),t_player->p_GetCentreY(),Float(t_i2->p_GetX()),Float(t_i2->p_GetY()))<t_closestDistWeapon){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<258>");
			t_closestWeapon=t_i2;
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<259>");
			t_closestDistWeapon=bb_BasicLibrary_findDist(t_player->p_GetCentreX(),t_player->p_GetCentreY(),Float(t_i2->p_GetX()),Float(t_i2->p_GetY()));
		}
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<263>");
	if(t_closestDistItem<30 && t_closestDistItem<t_closestDistWeapon){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<264>");
		m_droppedItems->p_RemoveFirst2(t_closestItem);
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<265>");
		t_closestItem->p_PickUp();
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<266>");
		t_player->p_AddItem(t_closestItem);
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<269>");
	if(t_closestDistWeapon<30 && t_closestDistWeapon<t_closestDistItem){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<270>");
		m_droppedWeapons->p_RemoveFirst3(t_closestWeapon);
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<271>");
		t_closestWeapon->p_PickUp();
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<272>");
		t_player->p_AddWeapon(t_closestWeapon);
	}
	return 0;
}
c_List3* c_Room::p_GetEnemyList(){
	DBG_ENTER("Room.GetEnemyList")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<513>");
	return m_enemyList;
}
int c_Room::p_Reset(){
	DBG_ENTER("Room.Reset")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<523>");
	this->m_Type=0;
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<524>");
	this->m_Neighbours=0;
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<525>");
	this->m_nDoor=String(L"0",1);
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<526>");
	this->m_eDoor=String(L"0",1);
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<527>");
	this->m_sDoor=String(L"0",1);
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<528>");
	this->m_wDoor=String(L"0",1);
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<529>");
	m_visible=false;
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<544>");
	int t_[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,2};
	int t_2[]={3,11,11,11,11,11,11,11,11,11,11,11,11,11,4};
	int t_3[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_4[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_5[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_6[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_7[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_8[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_9[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_10[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_11[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_12[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_13[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_14[]={3,7,7,7,7,7,7,7,7,7,7,7,7,7,4};
	int t_15[]={5,10,10,10,10,10,10,10,10,10,10,10,10,10,6};
	Array<int > t_16[]={Array<int >(t_,15),Array<int >(t_2,15),Array<int >(t_3,15),Array<int >(t_4,15),Array<int >(t_5,15),Array<int >(t_6,15),Array<int >(t_7,15),Array<int >(t_8,15),Array<int >(t_9,15),Array<int >(t_10,15),Array<int >(t_11,15),Array<int >(t_12,15),Array<int >(t_13,15),Array<int >(t_14,15),Array<int >(t_15,15)};
	gc_assign(m_WallLayout,Array<Array<int > >(t_16,15));
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<559>");
	int t_17[]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,2};
	int t_18[]={3,9,9,9,9,9,9,9,9,9,9,9,9,9,4};
	int t_19[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_20[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_21[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_22[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_23[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_24[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_25[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_26[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_27[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_28[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_29[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_30[]={3,8,8,8,8,8,8,8,8,8,8,8,8,8,4};
	int t_31[]={5,10,10,10,10,10,10,10,10,10,10,10,10,10,6};
	Array<int > t_32[]={Array<int >(t_17,15),Array<int >(t_18,15),Array<int >(t_19,15),Array<int >(t_20,15),Array<int >(t_21,15),Array<int >(t_22,15),Array<int >(t_23,15),Array<int >(t_24,15),Array<int >(t_25,15),Array<int >(t_26,15),Array<int >(t_27,15),Array<int >(t_28,15),Array<int >(t_29,15),Array<int >(t_30,15),Array<int >(t_31,15)};
	gc_assign(m_FloorLayout,Array<Array<int > >(t_32,15));
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<574>");
	int t_33[]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
	int t_34[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_35[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_36[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_37[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_38[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_39[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_40[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_41[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_42[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_43[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_44[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_45[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_46[]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
	int t_47[]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
	Array<int > t_48[]={Array<int >(t_33,15),Array<int >(t_34,15),Array<int >(t_35,15),Array<int >(t_36,15),Array<int >(t_37,15),Array<int >(t_38,15),Array<int >(t_39,15),Array<int >(t_40,15),Array<int >(t_41,15),Array<int >(t_42,15),Array<int >(t_43,15),Array<int >(t_44,15),Array<int >(t_45,15),Array<int >(t_46,15),Array<int >(t_47,15)};
	gc_assign(m_CollisionArray,Array<Array<int > >(t_48,15));
	return 0;
}
int c_Room::p_DrawFloor(){
	DBG_ENTER("Room.DrawFloor")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<88>");
	for(int t_x=0;t_x<=m_RoomSize32-1;t_x=t_x+1){
		DBG_BLOCK();
		DBG_LOCAL(t_x,"x")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<89>");
		for(int t_y=0;t_y<=m_RoomSize32-1;t_y=t_y+1){
			DBG_BLOCK();
			DBG_LOCAL(t_y,"y")
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<90>");
			if(m_FloorLayout.At(t_y).At(t_x)==1){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<91>");
				c_Image* t_floor=m_RoomTiles->p_GrabImage(64,288,32,32,1,c_Image::m_DefaultFlags);
				DBG_LOCAL(t_floor,"floor")
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<92>");
				bb_graphics_DrawImage(t_floor,Float(t_x*32),Float(t_y*32),0);
			}
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<94>");
			if(m_FloorLayout.At(t_y).At(t_x)==2){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<95>");
				c_Image* t_spike=m_RoomTiles->p_GrabImage(64,256,32,32,1,c_Image::m_DefaultFlags);
				DBG_LOCAL(t_spike,"spike")
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<96>");
				bb_graphics_DrawImage(t_spike,Float(t_x*32),Float(t_y*32),0);
			}
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<98>");
			if(m_FloorLayout.At(t_y).At(t_x)==3){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<99>");
				c_Image* t_topBrick=m_RoomTiles->p_GrabImage(32,352,32,32,1,c_Image::m_DefaultFlags);
				DBG_LOCAL(t_topBrick,"topBrick")
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<100>");
				bb_graphics_DrawImage(t_topBrick,Float(t_x*32),Float(t_y*32),0);
			}
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<102>");
			if(m_FloorLayout.At(t_y).At(t_x)==11){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<103>");
				c_Image* t_singleBox1=m_RoomTiles->p_GrabImage(32,224,32,32,1,c_Image::m_DefaultFlags);
				DBG_LOCAL(t_singleBox1,"singleBox1")
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<104>");
				bb_graphics_DrawImage(t_singleBox1,Float(t_x*32),Float(t_y*32),0);
			}
		}
	}
	return 0;
}
int c_Room::p_DrawEnemies(){
	DBG_ENTER("Room.DrawEnemies")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<150>");
	int t_debugTextCount=0;
	DBG_LOCAL(t_debugTextCount,"debugTextCount")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<151>");
	c_Enumerator2* t_=m_enemyList->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Enemy* t_i=t_->p_NextObject();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<152>");
		t_i->p_Draw2();
	}
	return 0;
}
int c_Room::p_DrawItems(){
	DBG_ENTER("Room.DrawItems")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<159>");
	c_Enumerator3* t_=m_droppedItems->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Interactables* t_i=t_->p_NextObject();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<160>");
		t_i->p_Draw2();
	}
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<162>");
	c_Enumerator4* t_2=m_droppedWeapons->p_ObjectEnumerator();
	while(t_2->p_HasNext()){
		DBG_BLOCK();
		c_WeaponInterface* t_i2=t_2->p_NextObject();
		DBG_LOCAL(t_i2,"i")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<163>");
		t_i2->p_DrawItem();
	}
	return 0;
}
int c_Room::p_DrawWalls(){
	DBG_ENTER("Room.DrawWalls")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<111>");
	for(int t_x=0;t_x<=m_RoomSize32-1;t_x=t_x+1){
		DBG_BLOCK();
		DBG_LOCAL(t_x,"x")
		DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<112>");
		for(int t_y=0;t_y<=m_RoomSize32-1;t_y=t_y+1){
			DBG_BLOCK();
			DBG_LOCAL(t_y,"y")
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<113>");
			int t_1=m_WallLayout.At(t_y).At(t_x);
			DBG_LOCAL(t_1,"1")
			DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<114>");
			if(t_1==0){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<115>");
				c_Image* t_topLeftWall=m_RoomTiles->p_GrabImage(0,320,32,32,1,c_Image::m_DefaultFlags);
				DBG_LOCAL(t_topLeftWall,"topLeftWall")
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<116>");
				bb_graphics_DrawImage(t_topLeftWall,Float(t_x*32),Float(t_y*32),0);
			}else{
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<117>");
				if(t_1==1){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<118>");
					c_Image* t_wall=m_RoomTiles->p_GrabImage(32,320,32,32,1,c_Image::m_DefaultFlags);
					DBG_LOCAL(t_wall,"wall")
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<119>");
					bb_graphics_DrawImage(t_wall,Float(t_x*32),Float(t_y*32),0);
				}else{
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<120>");
					if(t_1==2){
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<121>");
						c_Image* t_topRightWall=m_RoomTiles->p_GrabImage(64,320,32,32,1,c_Image::m_DefaultFlags);
						DBG_LOCAL(t_topRightWall,"topRightWall")
						DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<122>");
						bb_graphics_DrawImage(t_topRightWall,Float(t_x*32),Float(t_y*32),0);
					}else{
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<123>");
						if(t_1==3){
							DBG_BLOCK();
							DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<124>");
							c_Image* t_leftWall=m_RoomTiles->p_GrabImage(0,352,32,32,1,c_Image::m_DefaultFlags);
							DBG_LOCAL(t_leftWall,"leftWall")
							DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<125>");
							bb_graphics_DrawImage(t_leftWall,Float(t_x*32),Float(t_y*32),0);
						}else{
							DBG_BLOCK();
							DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<126>");
							if(t_1==4){
								DBG_BLOCK();
								DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<127>");
								c_Image* t_rightWall=m_RoomTiles->p_GrabImage(64,352,32,32,1,c_Image::m_DefaultFlags);
								DBG_LOCAL(t_rightWall,"rightWall")
								DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<128>");
								bb_graphics_DrawImage(t_rightWall,Float(t_x*32),Float(t_y*32),0);
							}else{
								DBG_BLOCK();
								DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<129>");
								if(t_1==5){
									DBG_BLOCK();
									DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<130>");
									c_Image* t_botLeftWall=m_RoomTiles->p_GrabImage(0,416,32,32,1,c_Image::m_DefaultFlags);
									DBG_LOCAL(t_botLeftWall,"botLeftWall")
									DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<131>");
									bb_graphics_DrawImage(t_botLeftWall,Float(t_x*32),Float(t_y*32),0);
								}else{
									DBG_BLOCK();
									DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<132>");
									if(t_1==6){
										DBG_BLOCK();
										DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<133>");
										c_Image* t_botRightWall=m_RoomTiles->p_GrabImage(64,416,32,32,1,c_Image::m_DefaultFlags);
										DBG_LOCAL(t_botRightWall,"botRightWall")
										DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<134>");
										bb_graphics_DrawImage(t_botRightWall,Float(t_x*32),Float(t_y*32),0);
									}else{
										DBG_BLOCK();
										DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<135>");
										if(t_1==7){
											DBG_BLOCK();
											DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<136>");
											c_Image* t_botWall=m_RoomTiles->p_GrabImage(32,384,32,32,1,c_Image::m_DefaultFlags);
											DBG_LOCAL(t_botWall,"botWall")
											DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<137>");
											bb_graphics_DrawImage(t_botWall,Float(t_x*32),Float(t_y*32),0);
										}else{
											DBG_BLOCK();
											DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<138>");
											if(t_1==10){
												DBG_BLOCK();
												DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<139>");
												c_Image* t_botBrick=m_RoomTiles->p_GrabImage(32,416,32,32,1,c_Image::m_DefaultFlags);
												DBG_LOCAL(t_botBrick,"botBrick")
												DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<140>");
												bb_graphics_DrawImage(t_botBrick,Float(t_x*32),Float(t_y*32),0);
											}else{
												DBG_BLOCK();
												DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<141>");
												if(t_1==12){
													DBG_BLOCK();
													DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<142>");
													c_Image* t_singleBox2=m_RoomTiles->p_GrabImage(32,192,32,32,1,c_Image::m_DefaultFlags);
													DBG_LOCAL(t_singleBox2,"singleBox2")
													DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<143>");
													bb_graphics_DrawImage(t_singleBox2,Float(t_x*32),Float(t_y*32),0);
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
	return 0;
}
bool c_Room::p_GetVisible(){
	DBG_ENTER("Room.GetVisible")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<505>");
	return m_visible;
}
bool c_Room::p_GetCurrent(){
	DBG_ENTER("Room.GetCurrent")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<509>");
	return m_current;
}
String c_Room::p_GetDoors(){
	DBG_ENTER("Room.GetDoors")
	c_Room *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<278>");
	String t_[]={m_nDoor,m_eDoor,m_sDoor,m_wDoor};
	gc_assign(m_DoorArray,Array<String >(t_,4));
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<279>");
	String t_2=String().Join(m_DoorArray);
	return t_2;
}
void c_Room::mark(){
	Object::mark();
	gc_mark_q(m_droppedItems);
	gc_mark_q(m_droppedWeapons);
	gc_mark_q(m_enemyList);
	gc_mark_q(m_WallLayout);
	gc_mark_q(m_FloorLayout);
	gc_mark_q(m_CollisionArray);
	gc_mark_q(m_RoomTiles);
	gc_mark_q(m_DoorArray);
}
String c_Room::debug(){
	String t="(Room)\n";
	t+=dbg_decl("Type",&m_Type);
	t+=dbg_decl("Neighbours",&m_Neighbours);
	t+=dbg_decl("x",&m_x);
	t+=dbg_decl("y",&m_y);
	t+=dbg_decl("nDoor",&m_nDoor);
	t+=dbg_decl("eDoor",&m_eDoor);
	t+=dbg_decl("wDoor",&m_wDoor);
	t+=dbg_decl("sDoor",&m_sDoor);
	t+=dbg_decl("DoorArray",&m_DoorArray);
	t+=dbg_decl("RoomTiles",&m_RoomTiles);
	t+=dbg_decl("WallLayout",&m_WallLayout);
	t+=dbg_decl("FloorLayout",&m_FloorLayout);
	t+=dbg_decl("CollisionArray",&m_CollisionArray);
	t+=dbg_decl("RoomSize32",&m_RoomSize32);
	t+=dbg_decl("visible",&m_visible);
	t+=dbg_decl("current",&m_current);
	t+=dbg_decl("complete",&m_complete);
	t+=dbg_decl("enemyList",&m_enemyList);
	t+=dbg_decl("enemyCount",&m_enemyCount);
	t+=dbg_decl("droppedItems",&m_droppedItems);
	t+=dbg_decl("droppedWeapons",&m_droppedWeapons);
	return t;
}
c_List::c_List(){
	m__head=((new c_HeadNode)->m_new());
}
c_List* c_List::m_new(){
	DBG_ENTER("List.new")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node2* c_List::p_AddLast(c_Interactables* t_data){
	DBG_ENTER("List.AddLast")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<108>");
	c_Node2* t_=(new c_Node2)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List* c_List::m_new2(Array<c_Interactables* > t_data){
	DBG_ENTER("List.new")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<13>");
	Array<c_Interactables* > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		c_Interactables* t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<14>");
		p_AddLast(t_t);
	}
	return this;
}
c_Enumerator3* c_List::p_ObjectEnumerator(){
	DBG_ENTER("List.ObjectEnumerator")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<186>");
	c_Enumerator3* t_=(new c_Enumerator3)->m_new(this);
	return t_;
}
bool c_List::p_IsEmpty(){
	DBG_ENTER("List.IsEmpty")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<50>");
	bool t_=m__head->m__succ==m__head;
	return t_;
}
c_Interactables* c_List::p_RemoveFirst(){
	DBG_ENTER("List.RemoveFirst")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<87>");
	if(p_IsEmpty()){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on empty list",31));
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<89>");
	c_Interactables* t_data=m__head->m__succ->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<90>");
	m__head->m__succ->p_Remove();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<91>");
	return t_data;
}
bool c_List::p_Equals(c_Interactables* t_lhs,c_Interactables* t_rhs){
	DBG_ENTER("List.Equals")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<28>");
	bool t_=t_lhs==t_rhs;
	return t_;
}
c_Node2* c_List::p_Find(c_Interactables* t_value,c_Node2* t_start){
	DBG_ENTER("List.Find")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_LOCAL(t_start,"start")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<116>");
	while(t_start!=m__head){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<117>");
		if(p_Equals(t_value,t_start->m__data)){
			DBG_BLOCK();
			return t_start;
		}
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<118>");
		t_start=t_start->m__succ;
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<120>");
	return 0;
}
c_Node2* c_List::p_Find2(c_Interactables* t_value){
	DBG_ENTER("List.Find")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<112>");
	c_Node2* t_=p_Find(t_value,m__head->m__succ);
	return t_;
}
void c_List::p_RemoveFirst2(c_Interactables* t_value){
	DBG_ENTER("List.RemoveFirst")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<141>");
	c_Node2* t_node=p_Find2(t_value);
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<142>");
	if((t_node)!=0){
		DBG_BLOCK();
		t_node->p_Remove();
	}
}
void c_List::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_Node2::c_Node2(){
	m__succ=0;
	m__pred=0;
	m__data=0;
}
c_Node2* c_Node2::m_new(c_Node2* t_succ,c_Node2* t_pred,c_Interactables* t_data){
	DBG_ENTER("Node.new")
	c_Node2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<265>");
	gc_assign(m__data,t_data);
	return this;
}
c_Node2* c_Node2::m_new2(){
	DBG_ENTER("Node.new")
	c_Node2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<258>");
	return this;
}
int c_Node2::p_Remove(){
	DBG_ENTER("Node.Remove")
	c_Node2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<274>");
	if(m__succ->m__pred!=this){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on removed node",33));
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<276>");
	gc_assign(m__succ->m__pred,m__pred);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<277>");
	gc_assign(m__pred->m__succ,m__succ);
	return 0;
}
void c_Node2::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
	gc_mark_q(m__data);
}
String c_Node2::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode::c_HeadNode(){
}
c_HeadNode* c_HeadNode::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<310>");
	c_Node2::m_new2();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode::mark(){
	c_Node2::mark();
}
String c_HeadNode::debug(){
	String t="(HeadNode)\n";
	t=c_Node2::debug()+t;
	return t;
}
c_List2::c_List2(){
	m__head=((new c_HeadNode2)->m_new());
}
c_List2* c_List2::m_new(){
	DBG_ENTER("List.new")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node3* c_List2::p_AddLast2(c_WeaponInterface* t_data){
	DBG_ENTER("List.AddLast")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<108>");
	c_Node3* t_=(new c_Node3)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List2* c_List2::m_new2(Array<c_WeaponInterface* > t_data){
	DBG_ENTER("List.new")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<13>");
	Array<c_WeaponInterface* > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		c_WeaponInterface* t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<14>");
		p_AddLast2(t_t);
	}
	return this;
}
c_Enumerator4* c_List2::p_ObjectEnumerator(){
	DBG_ENTER("List.ObjectEnumerator")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<186>");
	c_Enumerator4* t_=(new c_Enumerator4)->m_new(this);
	return t_;
}
bool c_List2::p_IsEmpty(){
	DBG_ENTER("List.IsEmpty")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<50>");
	bool t_=m__head->m__succ==m__head;
	return t_;
}
c_WeaponInterface* c_List2::p_RemoveFirst(){
	DBG_ENTER("List.RemoveFirst")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<87>");
	if(p_IsEmpty()){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on empty list",31));
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<89>");
	c_WeaponInterface* t_data=m__head->m__succ->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<90>");
	m__head->m__succ->p_Remove();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<91>");
	return t_data;
}
bool c_List2::p_Equals2(c_WeaponInterface* t_lhs,c_WeaponInterface* t_rhs){
	DBG_ENTER("List.Equals")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<28>");
	bool t_=t_lhs==t_rhs;
	return t_;
}
c_Node3* c_List2::p_Find3(c_WeaponInterface* t_value,c_Node3* t_start){
	DBG_ENTER("List.Find")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_LOCAL(t_start,"start")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<116>");
	while(t_start!=m__head){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<117>");
		if(p_Equals2(t_value,t_start->m__data)){
			DBG_BLOCK();
			return t_start;
		}
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<118>");
		t_start=t_start->m__succ;
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<120>");
	return 0;
}
c_Node3* c_List2::p_Find4(c_WeaponInterface* t_value){
	DBG_ENTER("List.Find")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<112>");
	c_Node3* t_=p_Find3(t_value,m__head->m__succ);
	return t_;
}
void c_List2::p_RemoveFirst3(c_WeaponInterface* t_value){
	DBG_ENTER("List.RemoveFirst")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<141>");
	c_Node3* t_node=p_Find4(t_value);
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<142>");
	if((t_node)!=0){
		DBG_BLOCK();
		t_node->p_Remove();
	}
}
void c_List2::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List2::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_Node3::c_Node3(){
	m__succ=0;
	m__pred=0;
	m__data=0;
}
c_Node3* c_Node3::m_new(c_Node3* t_succ,c_Node3* t_pred,c_WeaponInterface* t_data){
	DBG_ENTER("Node.new")
	c_Node3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<265>");
	gc_assign(m__data,t_data);
	return this;
}
c_Node3* c_Node3::m_new2(){
	DBG_ENTER("Node.new")
	c_Node3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<258>");
	return this;
}
int c_Node3::p_Remove(){
	DBG_ENTER("Node.Remove")
	c_Node3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<274>");
	if(m__succ->m__pred!=this){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on removed node",33));
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<276>");
	gc_assign(m__succ->m__pred,m__pred);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<277>");
	gc_assign(m__pred->m__succ,m__succ);
	return 0;
}
void c_Node3::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
	gc_mark_q(m__data);
}
String c_Node3::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode2::c_HeadNode2(){
}
c_HeadNode2* c_HeadNode2::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<310>");
	c_Node3::m_new2();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode2::mark(){
	c_Node3::mark();
}
String c_HeadNode2::debug(){
	String t="(HeadNode)\n";
	t=c_Node3::debug()+t;
	return t;
}
c_Enemy::c_Enemy(){
	m_type=0;
	m_maxHealth=FLOAT(.0);
	m_curHealth=FLOAT(.0);
	m_speed=FLOAT(.0);
	m_range=0;
	m_width=16;
	m_height=24;
	m_cd=0;
	m_x=FLOAT(.0);
	m_y=FLOAT(.0);
	m_active=false;
	m_idle=0;
	m_right=0;
	m_left=0;
	m_summon=0;
	m_vecSpeed=0;
	m_knockSpeed=FLOAT(-4.0);
	m_knockback=false;
	m_animState=String(L"IDLE",4);
	m_playerDist=0;
	m_charge=false;
	m_chargeTime=0;
	m_cdTime=0;
}
c_Enemy* c_Enemy::m_new(int t__type,Float t__x,Float t__y,bool t__active){
	DBG_ENTER("Enemy.new")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__type,"_type")
	DBG_LOCAL(t__x,"_x")
	DBG_LOCAL(t__y,"_y")
	DBG_LOCAL(t__active,"_active")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<31>");
	Float t_scaleX=FLOAT(.0);
	DBG_LOCAL(t_scaleX,"scaleX")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<32>");
	Float t_scaleY=FLOAT(.0);
	DBG_LOCAL(t_scaleY,"scaleY")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<33>");
	m_type=t__type;
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<34>");
	int t_1=m_type;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<35>");
	if(t_1==1){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<36>");
		m_maxHealth=FLOAT(2.0);
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<37>");
		m_curHealth=FLOAT(2.0);
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<38>");
		m_speed=FLOAT(1.5);
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<39>");
		m_range=200;
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<40>");
		m_width=16;
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<41>");
		m_height=24;
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<42>");
		m_cd=2000;
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<43>");
		t_scaleX=FLOAT(1.0);
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<44>");
		t_scaleY=FLOAT(1.0);
	}else{
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<45>");
		if(t_1==2){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<46>");
			m_maxHealth=FLOAT(3.0);
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<47>");
			m_curHealth=FLOAT(3.0);
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<48>");
			m_speed=FLOAT(0.5);
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<49>");
			m_range=300;
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<50>");
			m_width=16;
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<51>");
			m_height=20;
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<52>");
			m_cd=4000;
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<53>");
			t_scaleX=FLOAT(1.5);
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<54>");
			t_scaleY=FLOAT(1.5);
		}else{
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<55>");
			if(t_1==3){
				DBG_BLOCK();
			}else{
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<56>");
				if(t_1==4){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<57>");
					m_maxHealth=FLOAT(1.0);
					DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<58>");
					m_curHealth=FLOAT(1.0);
					DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<59>");
					m_speed=FLOAT(2.0);
					DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<60>");
					m_range=300;
					DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<61>");
					m_cd=1000;
					DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<62>");
					m_width=16;
					DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<63>");
					m_height=16;
					DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<64>");
					t_scaleX=FLOAT(1.0);
					DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<65>");
					t_scaleY=FLOAT(1.0);
				}else{
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<66>");
					if(t_1==10){
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<67>");
						m_maxHealth=FLOAT(10.0);
						DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<68>");
						m_curHealth=FLOAT(10.0);
						DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<69>");
						m_speed=FLOAT(1.0);
						DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<70>");
						m_range=500;
						DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<71>");
						m_cd=1000;
						DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<72>");
						m_width=32;
						DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<73>");
						m_height=36;
						DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<74>");
						t_scaleX=FLOAT(2.0);
						DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<75>");
						t_scaleY=FLOAT(2.0);
					}
				}
			}
		}
	}
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<77>");
	m_x=t__x;
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<78>");
	m_y=t__y;
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<79>");
	m_active=t__active;
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<80>");
	gc_assign(m_idle,(new c_Animation)->m_new(String(L"characters/enemies/",19)+String(m_type)+String(L"i.png",5),m_width,m_height,4,100,t_scaleX,t_scaleY));
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<81>");
	gc_assign(m_right,(new c_Animation)->m_new(String(L"characters/enemies/",19)+String(m_type)+String(L"r.png",5),m_width,m_height,4,100,t_scaleX,t_scaleY));
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<82>");
	gc_assign(m_left,(new c_Animation)->m_new(String(L"characters/enemies/",19)+String(m_type)+String(L"l.png",5),m_width,m_height,4,100,t_scaleX,t_scaleY));
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<83>");
	gc_assign(m_summon,(new c_Animation)->m_new(String(L"effects/summon.png",18),16,20,4,100,t_scaleX,t_scaleY));
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<84>");
	gc_assign(m_vecSpeed,(new c_Vec2D)->m_new(FLOAT(0.0),FLOAT(0.0)));
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<85>");
	m_width=int(Float(m_width)*t_scaleX);
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<86>");
	m_height=int(Float(m_height)*t_scaleY);
	return this;
}
c_Enemy* c_Enemy::m_new2(){
	DBG_ENTER("Enemy.new")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<5>");
	return this;
}
int c_Enemy::p_CheckActive(Float t_playerX,Float t_playerY){
	DBG_ENTER("Enemy.CheckActive")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_playerX,"playerX")
	DBG_LOCAL(t_playerY,"playerY")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<116>");
	Float t_xDist=t_playerX-(m_x+Float(m_width/2));
	DBG_LOCAL(t_xDist,"xDist")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<117>");
	Float t_yDist=t_playerY-(m_y+Float(m_width/2));
	DBG_LOCAL(t_yDist,"yDist")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<118>");
	int t_Dist=int((Float)sqrt(t_xDist*t_xDist+t_yDist*t_yDist));
	DBG_LOCAL(t_Dist,"Dist")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<119>");
	if(t_Dist<m_range){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<120>");
		m_active=true;
	}
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<123>");
	if(m_knockSpeed>=FLOAT(0.0)){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<124>");
		m_knockback=false;
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<125>");
		m_knockSpeed=FLOAT(-4.0);
	}else{
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<126>");
		if(m_knockSpeed>=FLOAT(-4.0) && m_knockSpeed<FLOAT(0.0) && m_knockback==true){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<127>");
			m_knockSpeed+=FLOAT(0.2);
		}
	}
	return 0;
}
int c_Enemy::p_CalcMove(Float t_playerX,Float t_playerY){
	DBG_ENTER("Enemy.CalcMove")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_playerX,"playerX")
	DBG_LOCAL(t_playerY,"playerY")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<134>");
	if(m_active==true && m_knockback==false){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<135>");
		m_vecSpeed->p_Reset();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<136>");
		m_vecSpeed->p_UpdateX2(t_playerX+FLOAT(15.0)-(m_x+Float(m_width/2)));
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<137>");
		m_vecSpeed->p_UpdateY2(t_playerY+FLOAT(32.0)-(m_y+Float(m_width/2)));
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<138>");
		m_vecSpeed->p_Normalise();
	}
	return 0;
}
int c_Enemy::p_MoveX(){
	DBG_ENTER("Enemy.MoveX")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<217>");
	if(m_vecSpeed->p_GetMagnitude()!=FLOAT(0.0) && m_knockback==false){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<218>");
		m_x+=m_vecSpeed->p_GetX()*m_speed;
	}
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<221>");
	if(m_vecSpeed->p_GetMagnitude()!=FLOAT(0.0) && m_knockback==true){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<222>");
		m_x+=m_vecSpeed->p_GetX()*m_knockSpeed;
	}
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<225>");
	if(m_vecSpeed->p_GetX()>FLOAT(0.0)){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<226>");
		m_animState=String(L"RIGHT",5);
	}else{
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<228>");
		m_animState=String(L"LEFT",4);
	}
	return 0;
}
int c_Enemy::p_GetX(){
	DBG_ENTER("Enemy.GetX")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<260>");
	int t_=int(m_x);
	return t_;
}
int c_Enemy::p_GetY(){
	DBG_ENTER("Enemy.GetY")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<264>");
	int t_=int(m_y);
	return t_;
}
int c_Enemy::p_GetWidth(){
	DBG_ENTER("Enemy.GetWidth")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<272>");
	return m_width;
}
int c_Enemy::p_GetHeight(){
	DBG_ENTER("Enemy.GetHeight")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<276>");
	return m_height;
}
int c_Enemy::p_CorrectX(){
	DBG_ENTER("Enemy.CorrectX")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<244>");
	if(m_knockback==false){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<245>");
		m_x-=m_vecSpeed->p_GetX()*m_speed;
	}else{
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<246>");
		if(m_knockback==true){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<247>");
			m_x-=m_vecSpeed->p_GetX()*m_knockSpeed;
		}
	}
	return 0;
}
int c_Enemy::p_MoveY(){
	DBG_ENTER("Enemy.MoveY")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<233>");
	if(m_vecSpeed->p_GetMagnitude()!=FLOAT(0.0) && m_knockback==false){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<234>");
		m_y+=m_vecSpeed->p_GetY()*m_speed;
	}
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<237>");
	if(m_vecSpeed->p_GetMagnitude()!=FLOAT(0.0) && m_knockback==true){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<238>");
		m_y+=m_vecSpeed->p_GetY()*m_knockSpeed;
	}
	return 0;
}
int c_Enemy::p_CorrectY(){
	DBG_ENTER("Enemy.CorrectY")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<252>");
	if(m_knockback==false){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<253>");
		m_y-=m_vecSpeed->p_GetY()*m_speed;
	}else{
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<254>");
		if(m_knockback==true){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<255>");
			m_y-=m_vecSpeed->p_GetY()*m_knockSpeed;
		}
	}
	return 0;
}
int c_Enemy::p_GetType(){
	DBG_ENTER("Enemy.GetType")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<268>");
	return m_type;
}
int c_Enemy::p_Attack3(c_Player* t_player){
	DBG_ENTER("Enemy.Attack")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_player,"player")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<172>");
	int t_centreX=int(m_x+Float(m_width/2));
	DBG_LOCAL(t_centreX,"centreX")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<173>");
	int t_centreY=int(m_y+Float(m_height/2));
	DBG_LOCAL(t_centreY,"centreY")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<174>");
	m_playerDist=bb_BasicLibrary_findDist(Float(t_centreX),Float(t_centreY),t_player->p_GetCentreX(),t_player->p_GetCentreY());
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<175>");
	int t_3=m_type;
	DBG_LOCAL(t_3,"3")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<176>");
	if(t_3==1){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<177>");
		if(m_charge==false){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<178>");
			m_chargeTime=bb_app_Millisecs()+2000;
		}
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<180>");
		if(bb_BasicLibrary_findDist(Float(t_centreX),Float(t_centreY),t_player->p_GetCentreX(),t_player->p_GetCentreY())<40){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<181>");
			m_charge=true;
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<182>");
			if(bb_app_Millisecs()>=m_cdTime && bb_app_Millisecs()>=m_chargeTime){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<183>");
				m_cdTime=bb_app_Millisecs()+m_cd;
				DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<184>");
				t_player->p_Hurt();
				DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<185>");
				m_charge=false;
			}
		}
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<188>");
		if(bb_BasicLibrary_findDist(Float(t_centreX),Float(t_centreY),t_player->p_GetCentreX(),t_player->p_GetCentreY())>45 && m_charge==true){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<189>");
			m_charge=false;
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<191>");
		if(t_3==2){
			DBG_BLOCK();
		}else{
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<193>");
			if(t_3==3){
				DBG_BLOCK();
			}else{
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<195>");
				if(t_3==4){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<196>");
					if(m_charge==false){
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<197>");
						m_chargeTime=bb_app_Millisecs()+1000;
					}
					DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<199>");
					if(bb_BasicLibrary_findDist(Float(t_centreX),Float(t_centreY),t_player->p_GetCentreX(),t_player->p_GetCentreY())<35){
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<200>");
						m_charge=true;
						DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<201>");
						if(bb_app_Millisecs()>=m_cdTime && bb_app_Millisecs()>=m_chargeTime){
							DBG_BLOCK();
							DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<202>");
							m_cdTime=bb_app_Millisecs()+m_cd;
							DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<203>");
							t_player->p_Hurt();
							DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<204>");
							m_charge=false;
						}
					}
					DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<207>");
					if(bb_BasicLibrary_findDist(Float(t_centreX),Float(t_centreY),t_player->p_GetCentreX(),t_player->p_GetCentreY())>35 && m_charge==true){
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<208>");
						m_charge=false;
					}
				}
			}
		}
	}
	return 0;
}
bool c_Enemy::p_Summon(c_Player* t_player){
	DBG_ENTER("Enemy.Summon")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_player,"player")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<150>");
	int t_centreX=int(m_x+FLOAT(7.0));
	DBG_LOCAL(t_centreX,"centreX")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<151>");
	int t_centreY=int(m_y+FLOAT(14.0));
	DBG_LOCAL(t_centreY,"centreY")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<152>");
	m_playerDist=bb_BasicLibrary_findDist(Float(t_centreX),Float(t_centreY),t_player->p_GetCentreX(),t_player->p_GetCentreY());
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<153>");
	if(m_charge==false){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<154>");
		m_chargeTime=bb_app_Millisecs()+1000;
	}
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<157>");
	if(bb_BasicLibrary_findDist(Float(t_centreX),Float(t_centreY),t_player->p_GetCentreX(),t_player->p_GetCentreY())<300){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<158>");
		m_charge=true;
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<159>");
		if(bb_app_Millisecs()>=m_cdTime && bb_app_Millisecs()>=m_chargeTime){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<160>");
			m_cdTime=bb_app_Millisecs()+m_cd;
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<161>");
			m_charge=false;
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<162>");
			return true;
		}
	}
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<165>");
	if(bb_BasicLibrary_findDist(Float(t_centreX),Float(t_centreY),t_player->p_GetCentreX(),t_player->p_GetCentreY())>350 && m_charge==true){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<166>");
		m_charge=false;
	}
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<168>");
	return false;
}
int c_Enemy::p_CheckHealth(){
	DBG_ENTER("Enemy.CheckHealth")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<284>");
	int t_=int(m_curHealth);
	return t_;
}
int c_Enemy::p_Draw2(){
	DBG_ENTER("Enemy.Draw")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<91>");
	if(m_type==2 && bb_app_Millisecs()>=m_cdTime-1500){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<92>");
		m_summon->p_Play(m_x,m_y);
	}
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<95>");
	String t_2=m_animState;
	DBG_LOCAL(t_2,"2")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<96>");
	if(t_2==String(L"IDLE",4)){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<97>");
		m_idle->p_Play(m_x,m_y);
	}else{
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<98>");
		if(t_2==String(L"RIGHT",5)){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<99>");
			m_right->p_Play(m_x,m_y);
		}else{
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<100>");
			if(t_2==String(L"LEFT",4)){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<101>");
				m_left->p_Play(m_x,m_y);
			}
		}
	}
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<105>");
	bb_graphics_SetColor(FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<106>");
	bb_graphics_DrawRect(m_x-FLOAT(1.0),m_y-FLOAT(8.5),Float(m_width+2),FLOAT(6.0));
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<107>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<108>");
	bb_graphics_DrawRect(m_x,m_y-FLOAT(7.5),Float(m_width),FLOAT(5.0));
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<109>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<110>");
	Float t_healthRatio=m_curHealth/m_maxHealth;
	DBG_LOCAL(t_healthRatio,"healthRatio")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<111>");
	bb_graphics_DrawRect(m_x,m_y-FLOAT(7.5),t_healthRatio*Float(m_width),FLOAT(5.0));
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<112>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	return 0;
}
int c_Enemy::p_Damage(int t_ax,int t_ay,int t_aw,int t_ah){
	DBG_ENTER("Enemy.Damage")
	c_Enemy *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_ax,"ax")
	DBG_LOCAL(t_ay,"ay")
	DBG_LOCAL(t_aw,"aw")
	DBG_LOCAL(t_ah,"ah")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<143>");
	if(bb_BasicLibrary_Collide(t_ax,t_ay,t_aw,t_ah,int(m_x),int(m_y),m_width,m_height)){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<144>");
		m_curHealth=m_curHealth-FLOAT(1.0);
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<145>");
		m_knockback=true;
	}
	return 0;
}
void c_Enemy::mark(){
	Object::mark();
	gc_mark_q(m_idle);
	gc_mark_q(m_right);
	gc_mark_q(m_left);
	gc_mark_q(m_summon);
	gc_mark_q(m_vecSpeed);
}
String c_Enemy::debug(){
	String t="(Enemy)\n";
	t+=dbg_decl("type",&m_type);
	t+=dbg_decl("maxHealth",&m_maxHealth);
	t+=dbg_decl("curHealth",&m_curHealth);
	t+=dbg_decl("speed",&m_speed);
	t+=dbg_decl("x",&m_x);
	t+=dbg_decl("y",&m_y);
	t+=dbg_decl("width",&m_width);
	t+=dbg_decl("height",&m_height);
	t+=dbg_decl("active",&m_active);
	t+=dbg_decl("range",&m_range);
	t+=dbg_decl("vecSpeed",&m_vecSpeed);
	t+=dbg_decl("idle",&m_idle);
	t+=dbg_decl("left",&m_left);
	t+=dbg_decl("right",&m_right);
	t+=dbg_decl("summon",&m_summon);
	t+=dbg_decl("animState",&m_animState);
	t+=dbg_decl("cd",&m_cd);
	t+=dbg_decl("cdTime",&m_cdTime);
	t+=dbg_decl("knockback",&m_knockback);
	t+=dbg_decl("knockSpeed",&m_knockSpeed);
	t+=dbg_decl("chargeTime",&m_chargeTime);
	t+=dbg_decl("charge",&m_charge);
	t+=dbg_decl("playerDist",&m_playerDist);
	return t;
}
c_List3::c_List3(){
	m__head=((new c_HeadNode3)->m_new());
}
c_List3* c_List3::m_new(){
	DBG_ENTER("List.new")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node4* c_List3::p_AddLast3(c_Enemy* t_data){
	DBG_ENTER("List.AddLast")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<108>");
	c_Node4* t_=(new c_Node4)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List3* c_List3::m_new2(Array<c_Enemy* > t_data){
	DBG_ENTER("List.new")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<13>");
	Array<c_Enemy* > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		c_Enemy* t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<14>");
		p_AddLast3(t_t);
	}
	return this;
}
int c_List3::p_Clear(){
	DBG_ENTER("List.Clear")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<36>");
	gc_assign(m__head->m__succ,m__head);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<37>");
	gc_assign(m__head->m__pred,m__head);
	return 0;
}
c_Enumerator2* c_List3::p_ObjectEnumerator(){
	DBG_ENTER("List.ObjectEnumerator")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<186>");
	c_Enumerator2* t_=(new c_Enumerator2)->m_new(this);
	return t_;
}
bool c_List3::p_Equals3(c_Enemy* t_lhs,c_Enemy* t_rhs){
	DBG_ENTER("List.Equals")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<28>");
	bool t_=t_lhs==t_rhs;
	return t_;
}
int c_List3::p_RemoveEach(c_Enemy* t_value){
	DBG_ENTER("List.RemoveEach")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<151>");
	c_Node4* t_node=m__head->m__succ;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<152>");
	while(t_node!=m__head){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<153>");
		c_Node4* t_succ=t_node->m__succ;
		DBG_LOCAL(t_succ,"succ")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<154>");
		if(p_Equals3(t_node->m__data,t_value)){
			DBG_BLOCK();
			t_node->p_Remove();
		}
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<155>");
		t_node=t_succ;
	}
	return 0;
}
void c_List3::p_Remove2(c_Enemy* t_value){
	DBG_ENTER("List.Remove")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<137>");
	p_RemoveEach(t_value);
}
void c_List3::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List3::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_Node4::c_Node4(){
	m__succ=0;
	m__pred=0;
	m__data=0;
}
c_Node4* c_Node4::m_new(c_Node4* t_succ,c_Node4* t_pred,c_Enemy* t_data){
	DBG_ENTER("Node.new")
	c_Node4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<265>");
	gc_assign(m__data,t_data);
	return this;
}
c_Node4* c_Node4::m_new2(){
	DBG_ENTER("Node.new")
	c_Node4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<258>");
	return this;
}
int c_Node4::p_Remove(){
	DBG_ENTER("Node.Remove")
	c_Node4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<274>");
	if(m__succ->m__pred!=this){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on removed node",33));
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<276>");
	gc_assign(m__succ->m__pred,m__pred);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<277>");
	gc_assign(m__pred->m__succ,m__succ);
	return 0;
}
void c_Node4::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
	gc_mark_q(m__data);
}
String c_Node4::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode3::c_HeadNode3(){
}
c_HeadNode3* c_HeadNode3::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<310>");
	c_Node4::m_new2();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode3::mark(){
	c_Node4::mark();
}
String c_HeadNode3::debug(){
	String t="(HeadNode)\n";
	t=c_Node4::debug()+t;
	return t;
}
Float bb_random_Rnd(){
	DBG_ENTER("Rnd")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/random.monkey<21>");
	bb_random_Seed=bb_random_Seed*1664525+1013904223|0;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/random.monkey<22>");
	Float t_=Float(bb_random_Seed>>8&16777215)/FLOAT(16777216.0);
	return t_;
}
Float bb_random_Rnd2(Float t_low,Float t_high){
	DBG_ENTER("Rnd")
	DBG_LOCAL(t_low,"low")
	DBG_LOCAL(t_high,"high")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/random.monkey<30>");
	Float t_=bb_random_Rnd3(t_high-t_low)+t_low;
	return t_;
}
Float bb_random_Rnd3(Float t_range){
	DBG_ENTER("Rnd")
	DBG_LOCAL(t_range,"range")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/random.monkey<26>");
	Float t_=bb_random_Rnd()*t_range;
	return t_;
}
c_Weapon::c_Weapon(){
	m_damage=0;
	m_state=String();
	m_id=0;
	m_guiImage=0;
	m_cdTime=0;
	m_cd=250;
}
c_Weapon* c_Weapon::m_new(){
	DBG_ENTER("Weapon.new")
	c_Weapon *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<105>");
	c_GameObject::m_new2();
	return this;
}
void c_Weapon::mark(){
	c_GameObject::mark();
	gc_mark_q(m_guiImage);
}
String c_Weapon::debug(){
	String t="(Weapon)\n";
	t=c_GameObject::debug()+t;
	t+=dbg_decl("id",&m_id);
	t+=dbg_decl("state",&m_state);
	t+=dbg_decl("guiImage",&m_guiImage);
	t+=dbg_decl("damage",&m_damage);
	t+=dbg_decl("cd",&m_cd);
	t+=dbg_decl("cdTime",&m_cdTime);
	return t;
}
c_Sword::c_Sword(){
	m_slashSound=0;
	m_slashRight=0;
	m_slashLeft=0;
	m_slashUp=0;
	m_slashDown=0;
	m_update=true;
	m_angle=0;
	m_targetAngle=0;
	m_damageWidth=64;
	m_damageHeight=32;
	m_hitbox=String(L"NONE",4);
	m_angleX=0;
	m_angleY=0;
}
c_Sword* c_Sword::m_new(int t__damage,String t__path,int t__width,int t__height,int t__id){
	DBG_ENTER("Sword.new")
	c_Sword *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__damage,"_damage")
	DBG_LOCAL(t__path,"_path")
	DBG_LOCAL(t__width,"_width")
	DBG_LOCAL(t__height,"_height")
	DBG_LOCAL(t__id,"_id")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<131>");
	c_Weapon::m_new();
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<132>");
	m_width=t__width;
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<133>");
	m_height=t__height;
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<134>");
	m_path=t__path;
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<135>");
	m_damage=t__damage;
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<136>");
	gc_assign(m_sprite,bb_graphics_LoadImage2(m_path,m_width,m_height,1,c_Image::m_DefaultFlags));
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<137>");
	m_state=String(L"Dropped",7);
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<138>");
	m_id=t__id;
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<139>");
	gc_assign(m_guiImage,bb_graphics_LoadImage(String(L"gui/items/",10)+String(m_id)+String(L".png",4),1,c_Image::m_DefaultFlags));
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<140>");
	m_x=FLOAT(320.0);
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<141>");
	m_y=FLOAT(240.0);
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<142>");
	gc_assign(m_slashSound,bb_audio_LoadSound(String(L"soundfx/sword.mp3",17)));
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<143>");
	gc_assign(m_slashRight,(new c_Animation)->m_new(String(L"effects/slashRight.png",22),32,64,3,75,FLOAT(1.0),FLOAT(1.0)));
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<144>");
	gc_assign(m_slashLeft,(new c_Animation)->m_new(String(L"effects/slashLeft.png",21),32,64,3,75,FLOAT(1.0),FLOAT(1.0)));
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<145>");
	gc_assign(m_slashUp,(new c_Animation)->m_new(String(L"effects/slashUp.png",19),64,32,3,75,FLOAT(1.0),FLOAT(1.0)));
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<146>");
	gc_assign(m_slashDown,(new c_Animation)->m_new(String(L"effects/slashDown.png",21),64,32,3,75,FLOAT(1.0),FLOAT(1.0)));
	return this;
}
c_Sword* c_Sword::m_new2(){
	DBG_ENTER("Sword.new")
	c_Sword *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<116>");
	c_Weapon::m_new();
	return this;
}
int c_Sword::p_Attack2(int t_px,int t_py,c_List3* t_target){
	DBG_ENTER("Sword.Attack")
	c_Sword *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_px,"px")
	DBG_LOCAL(t_py,"py")
	DBG_LOCAL(t_target,"target")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<150>");
	if(((bb_input_MouseHit(0))!=0) && bb_app_Millisecs()>=m_cdTime){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<151>");
		m_update=false;
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<152>");
		m_targetAngle=m_angle-270;
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<153>");
		m_cdTime=bb_app_Millisecs()+m_cd;
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<154>");
		bb_audio_PlaySound(m_slashSound,1,0);
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<155>");
		c_Enumerator2* t_=t_target->p_ObjectEnumerator();
		while(t_->p_HasNext()){
			DBG_BLOCK();
			c_Enemy* t_i=t_->p_NextObject();
			DBG_LOCAL(t_i,"i")
			DBG_INFO("F:/Final Game/GameModules/Items.monkey<156>");
			if(m_angle<-45 && m_angle>-90 || m_angle<270 && m_angle>235){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/Items.monkey<157>");
				t_i->p_Damage(t_px-(m_damageWidth-10)/2+6,t_py-m_damageHeight+18,m_damageWidth,m_damageHeight);
				DBG_INFO("F:/Final Game/GameModules/Items.monkey<158>");
				m_hitbox=String(L"UP",2);
			}else{
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/Items.monkey<159>");
				if(m_angle<235 && m_angle>125){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/Items.monkey<160>");
					t_i->p_Damage(t_px+28,t_py-(m_damageWidth-30)/2+16,m_damageHeight,m_damageWidth);
					DBG_INFO("F:/Final Game/GameModules/Items.monkey<161>");
					m_hitbox=String(L"RIGHT",5);
				}else{
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/Items.monkey<162>");
					if(m_angle<125 && m_angle>45){
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/GameModules/Items.monkey<163>");
						t_i->p_Damage(t_px-(m_damageWidth-10)/2+6,t_py+48,m_damageWidth,m_damageHeight);
						DBG_INFO("F:/Final Game/GameModules/Items.monkey<164>");
						m_hitbox=String(L"DOWN",4);
					}else{
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/GameModules/Items.monkey<165>");
						if(m_angle<45 && m_angle>-45){
							DBG_BLOCK();
							DBG_INFO("F:/Final Game/GameModules/Items.monkey<166>");
							t_i->p_Damage(t_px-m_damageHeight+6,t_py-(m_damageWidth-30)/2+16,m_damageHeight,m_damageWidth);
							DBG_INFO("F:/Final Game/GameModules/Items.monkey<167>");
							m_hitbox=String(L"LEFT",4);
						}
					}
				}
			}
		}
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<171>");
		if(m_angle<-45 && m_angle>-90 || m_angle<270 && m_angle>235){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/Items.monkey<172>");
			m_hitbox=String(L"UP",2);
		}else{
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/Items.monkey<173>");
			if(m_angle<235 && m_angle>125){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/Items.monkey<174>");
				m_hitbox=String(L"RIGHT",5);
			}else{
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/Items.monkey<175>");
				if(m_angle<125 && m_angle>45){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/Items.monkey<176>");
					m_hitbox=String(L"DOWN",4);
				}else{
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/Items.monkey<177>");
					if(m_angle<45 && m_angle>-45){
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/GameModules/Items.monkey<178>");
						m_hitbox=String(L"LEFT",4);
					}
				}
			}
		}
	}
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<183>");
	if(m_angle<=m_targetAngle){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<184>");
		m_update=true;
	}
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<187>");
	if(m_update==true){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<188>");
		m_angleX=int(bb_input_MouseX()-Float(t_px+14));
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<189>");
		m_angleY=int(bb_input_MouseY()-Float(t_py+25));
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<190>");
		m_angle=int((Float)(atan2(Float(m_angleX),Float(m_angleY))*R2D)+FLOAT(90.0));
	}else{
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<192>");
		m_angle-=30;
	}
	return 0;
}
int c_Sword::p_DrawItem(){
	DBG_ENTER("Sword.DrawItem")
	c_Sword *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<197>");
	if(m_state==String(L"Dropped",7)){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<199>");
		bb_graphics_DrawImage(m_guiImage,m_x,m_y,0);
	}
	return 0;
}
int c_Sword::p_Draw(int t__x,int t__y){
	DBG_ENTER("Sword.Draw")
	c_Sword *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__x,"_x")
	DBG_LOCAL(t__y,"_y")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<208>");
	if(m_state==String(L"Equipped",8)){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<209>");
		bb_graphics_DrawImage(m_guiImage,FLOAT(500.0),FLOAT(442.0),0);
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<210>");
		int t_yOff=int(FLOAT(0.0)*(Float)cos((Float(m_angle))*D2R)-FLOAT(-5.0)*(Float)sin((Float(m_angle))*D2R));
		DBG_LOCAL(t_yOff,"yOff")
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<211>");
		int t_xOff=int(FLOAT(-5.0)*(Float)cos((Float(m_angle))*D2R)+FLOAT(0.0)*(Float)sin((Float(m_angle))*D2R));
		DBG_LOCAL(t_xOff,"xOff")
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<212>");
		bb_graphics_DrawImage2(m_sprite,Float(t__x+10+t_xOff),Float(t__y+30+t_yOff),Float(m_angle),FLOAT(1.25),FLOAT(1.25),0);
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<213>");
		if(bb_app_Millisecs()<=m_cdTime){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/Items.monkey<214>");
			String t_1=m_hitbox;
			DBG_LOCAL(t_1,"1")
			DBG_INFO("F:/Final Game/GameModules/Items.monkey<215>");
			if(t_1==String(L"UP",2)){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/Items.monkey<216>");
				m_slashUp->p_Play(Float(t__x-(m_damageWidth-10)/2+6),Float(t__y-m_damageHeight+18));
			}else{
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/Items.monkey<218>");
				if(t_1==String(L"LEFT",4)){
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/Items.monkey<219>");
					m_slashLeft->p_Play(Float(t__x-m_damageHeight+6),Float(t__y-(m_damageWidth-30)/2+16));
				}else{
					DBG_BLOCK();
					DBG_INFO("F:/Final Game/GameModules/Items.monkey<221>");
					if(t_1==String(L"DOWN",4)){
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/GameModules/Items.monkey<222>");
						m_slashDown->p_Play(Float(t__x-(m_damageWidth-10)/2+6),Float(t__y+48));
					}else{
						DBG_BLOCK();
						DBG_INFO("F:/Final Game/GameModules/Items.monkey<224>");
						if(t_1==String(L"RIGHT",5)){
							DBG_BLOCK();
							DBG_INFO("F:/Final Game/GameModules/Items.monkey<225>");
							m_slashRight->p_Play(Float(t__x+28),Float(t__y-(m_damageWidth-30)/2+16));
						}
					}
				}
			}
		}
	}
	return 0;
}
int c_Sword::p_GetX(){
	DBG_ENTER("Sword.GetX")
	c_Sword *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<233>");
	int t_=int(m_x);
	return t_;
}
int c_Sword::p_GetY(){
	DBG_ENTER("Sword.GetY")
	c_Sword *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<237>");
	int t_=int(m_y);
	return t_;
}
int c_Sword::p_PickUp(){
	DBG_ENTER("Sword.PickUp")
	c_Sword *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<241>");
	m_state=String(L"Equipped",8);
	return 0;
}
void c_Sword::mark(){
	c_Weapon::mark();
	gc_mark_q(m_slashSound);
	gc_mark_q(m_slashRight);
	gc_mark_q(m_slashLeft);
	gc_mark_q(m_slashUp);
	gc_mark_q(m_slashDown);
}
String c_Sword::debug(){
	String t="(Sword)\n";
	t=c_Weapon::debug()+t;
	t+=dbg_decl("angle",&m_angle);
	t+=dbg_decl("angleX",&m_angleX);
	t+=dbg_decl("angleY",&m_angleY);
	t+=dbg_decl("targetAngle",&m_targetAngle);
	t+=dbg_decl("update",&m_update);
	t+=dbg_decl("hitbox",&m_hitbox);
	t+=dbg_decl("damageWidth",&m_damageWidth);
	t+=dbg_decl("damageHeight",&m_damageHeight);
	t+=dbg_decl("slashSound",&m_slashSound);
	t+=dbg_decl("slashRight",&m_slashRight);
	t+=dbg_decl("slashLeft",&m_slashLeft);
	t+=dbg_decl("slashUp",&m_slashUp);
	t+=dbg_decl("slashDown",&m_slashDown);
	return t;
}
c_Vec2D::c_Vec2D(){
	m_VecX=FLOAT(.0);
	m_VecY=FLOAT(.0);
	m_magnitude=FLOAT(.0);
}
c_Vec2D* c_Vec2D::m_new(Float t_x,Float t_y){
	DBG_ENTER("Vec2D.new")
	c_Vec2D *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<89>");
	m_VecX=t_x;
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<90>");
	m_VecY=t_y;
	return this;
}
c_Vec2D* c_Vec2D::m_new2(){
	DBG_ENTER("Vec2D.new")
	c_Vec2D *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<83>");
	return this;
}
int c_Vec2D::p_Reset(){
	DBG_ENTER("Vec2D.Reset")
	c_Vec2D *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<115>");
	m_VecX=FLOAT(0.0);
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<116>");
	m_VecY=FLOAT(0.0);
	return 0;
}
int c_Vec2D::p_UpdateX2(Float t_x){
	DBG_ENTER("Vec2D.UpdateX")
	c_Vec2D *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<94>");
	m_VecX=t_x;
	return 0;
}
int c_Vec2D::p_UpdateY2(Float t_y){
	DBG_ENTER("Vec2D.UpdateY")
	c_Vec2D *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<98>");
	m_VecY=t_y;
	return 0;
}
int c_Vec2D::p_Normalise(){
	DBG_ENTER("Vec2D.Normalise")
	c_Vec2D *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<120>");
	m_magnitude=(Float)sqrt(m_VecX*m_VecX+m_VecY*m_VecY);
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<121>");
	m_VecX/=m_magnitude;
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<122>");
	m_VecY/=m_magnitude;
	return 0;
}
Float c_Vec2D::p_GetMagnitude(){
	DBG_ENTER("Vec2D.GetMagnitude")
	c_Vec2D *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<102>");
	return m_magnitude;
}
Float c_Vec2D::p_GetX(){
	DBG_ENTER("Vec2D.GetX")
	c_Vec2D *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<107>");
	return m_VecX;
}
Float c_Vec2D::p_GetY(){
	DBG_ENTER("Vec2D.GetY")
	c_Vec2D *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<111>");
	return m_VecY;
}
void c_Vec2D::mark(){
	Object::mark();
}
String c_Vec2D::debug(){
	String t="(Vec2D)\n";
	t+=dbg_decl("VecX",&m_VecX);
	t+=dbg_decl("VecY",&m_VecY);
	t+=dbg_decl("magnitude",&m_magnitude);
	return t;
}
c_List4::c_List4(){
	m__head=((new c_HeadNode4)->m_new());
}
c_List4* c_List4::m_new(){
	DBG_ENTER("List.new")
	c_List4 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node5* c_List4::p_AddLast4(c_Room* t_data){
	DBG_ENTER("List.AddLast")
	c_List4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<108>");
	c_Node5* t_=(new c_Node5)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List4* c_List4::m_new2(Array<c_Room* > t_data){
	DBG_ENTER("List.new")
	c_List4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<13>");
	Array<c_Room* > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		c_Room* t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<14>");
		p_AddLast4(t_t);
	}
	return this;
}
int c_List4::p_Count(){
	DBG_ENTER("List.Count")
	c_List4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<41>");
	int t_n=0;
	c_Node5* t_node=m__head->m__succ;
	DBG_LOCAL(t_n,"n")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<42>");
	while(t_node!=m__head){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<43>");
		t_node=t_node->m__succ;
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<44>");
		t_n+=1;
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<46>");
	return t_n;
}
c_Enumerator* c_List4::p_ObjectEnumerator(){
	DBG_ENTER("List.ObjectEnumerator")
	c_List4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<186>");
	c_Enumerator* t_=(new c_Enumerator)->m_new(this);
	return t_;
}
Array<c_Room* > c_List4::p_ToArray(){
	DBG_ENTER("List.ToArray")
	c_List4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<19>");
	Array<c_Room* > t_arr=Array<c_Room* >(p_Count());
	int t_i=0;
	DBG_LOCAL(t_arr,"arr")
	DBG_LOCAL(t_i,"i")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<20>");
	c_Enumerator* t_=this->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Room* t_t=t_->p_NextObject();
		DBG_LOCAL(t_t,"t")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<21>");
		gc_assign(t_arr.At(t_i),t_t);
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<22>");
		t_i+=1;
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<24>");
	return t_arr;
}
void c_List4::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List4::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_Node5::c_Node5(){
	m__succ=0;
	m__pred=0;
	m__data=0;
}
c_Node5* c_Node5::m_new(c_Node5* t_succ,c_Node5* t_pred,c_Room* t_data){
	DBG_ENTER("Node.new")
	c_Node5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<265>");
	gc_assign(m__data,t_data);
	return this;
}
c_Node5* c_Node5::m_new2(){
	DBG_ENTER("Node.new")
	c_Node5 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<258>");
	return this;
}
void c_Node5::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
	gc_mark_q(m__data);
}
String c_Node5::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode4::c_HeadNode4(){
}
c_HeadNode4* c_HeadNode4::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<310>");
	c_Node5::m_new2();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode4::mark(){
	c_Node5::mark();
}
String c_HeadNode4::debug(){
	String t="(HeadNode)\n";
	t=c_Node5::debug()+t;
	return t;
}
c_Enumerator::c_Enumerator(){
	m__list=0;
	m__curr=0;
}
c_Enumerator* c_Enumerator::m_new(c_List4* t_list){
	DBG_ENTER("Enumerator.new")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<326>");
	gc_assign(m__list,t_list);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<327>");
	gc_assign(m__curr,t_list->m__head->m__succ);
	return this;
}
c_Enumerator* c_Enumerator::m_new2(){
	DBG_ENTER("Enumerator.new")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<323>");
	return this;
}
bool c_Enumerator::p_HasNext(){
	DBG_ENTER("Enumerator.HasNext")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<331>");
	while(m__curr->m__succ->m__pred!=m__curr){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<332>");
		gc_assign(m__curr,m__curr->m__succ);
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<334>");
	bool t_=m__curr!=m__list->m__head;
	return t_;
}
c_Room* c_Enumerator::p_NextObject(){
	DBG_ENTER("Enumerator.NextObject")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<338>");
	c_Room* t_data=m__curr->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<339>");
	gc_assign(m__curr,m__curr->m__succ);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<340>");
	return t_data;
}
void c_Enumerator::mark(){
	Object::mark();
	gc_mark_q(m__list);
	gc_mark_q(m__curr);
}
String c_Enumerator::debug(){
	String t="(Enumerator)\n";
	t+=dbg_decl("_list",&m__list);
	t+=dbg_decl("_curr",&m__curr);
	return t;
}
void bb_app_HideMouse(){
	DBG_ENTER("HideMouse")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/app.monkey<251>");
	bb_app__game->SetMouseVisible(false);
}
c_Enumerator2::c_Enumerator2(){
	m__list=0;
	m__curr=0;
}
c_Enumerator2* c_Enumerator2::m_new(c_List3* t_list){
	DBG_ENTER("Enumerator.new")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<326>");
	gc_assign(m__list,t_list);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<327>");
	gc_assign(m__curr,t_list->m__head->m__succ);
	return this;
}
c_Enumerator2* c_Enumerator2::m_new2(){
	DBG_ENTER("Enumerator.new")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<323>");
	return this;
}
bool c_Enumerator2::p_HasNext(){
	DBG_ENTER("Enumerator.HasNext")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<331>");
	while(m__curr->m__succ->m__pred!=m__curr){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<332>");
		gc_assign(m__curr,m__curr->m__succ);
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<334>");
	bool t_=m__curr!=m__list->m__head;
	return t_;
}
c_Enemy* c_Enumerator2::p_NextObject(){
	DBG_ENTER("Enumerator.NextObject")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<338>");
	c_Enemy* t_data=m__curr->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<339>");
	gc_assign(m__curr,m__curr->m__succ);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<340>");
	return t_data;
}
void c_Enumerator2::mark(){
	Object::mark();
	gc_mark_q(m__list);
	gc_mark_q(m__curr);
}
String c_Enumerator2::debug(){
	String t="(Enumerator)\n";
	t+=dbg_decl("_list",&m__list);
	t+=dbg_decl("_curr",&m__curr);
	return t;
}
bool bb_BasicLibrary_Collide(int t_x,int t_y,int t_w,int t_h,int t_x1,int t_y1,int t_w1,int t_h1){
	DBG_ENTER("Collide")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_w,"w")
	DBG_LOCAL(t_h,"h")
	DBG_LOCAL(t_x1,"x1")
	DBG_LOCAL(t_y1,"y1")
	DBG_LOCAL(t_w1,"w1")
	DBG_LOCAL(t_h1,"h1")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<162>");
	if(t_x<t_x1+t_w1 && t_x+t_w>t_x1 && t_y<t_y1+t_h1 && t_y+t_h>t_y1){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<163>");
		return true;
	}else{
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<165>");
		return false;
	}
}
bool bb_Enemies_CheckCollision(c_Enemy* t_e,c_List3* t_list){
	DBG_ENTER("CheckCollision")
	DBG_LOCAL(t_e,"e")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<294>");
	c_Enumerator2* t_=t_list->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_Enemy* t_l=t_->p_NextObject();
		DBG_LOCAL(t_l,"l")
		DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<295>");
		if(t_e!=t_l){
			DBG_BLOCK();
			DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<296>");
			if(bb_BasicLibrary_Collide(t_e->p_GetX(),t_e->p_GetY(),t_e->p_GetWidth(),t_e->p_GetHeight(),t_l->p_GetX(),t_l->p_GetY(),t_l->p_GetWidth(),t_l->p_GetHeight())==true){
				DBG_BLOCK();
				DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<297>");
				return true;
			}
		}
	}
	DBG_INFO("F:/Final Game/GameModules/Enemies.monkey<301>");
	return false;
}
int bb_BasicLibrary_findDist(Float t_x1,Float t_y1,Float t_x2,Float t_y2){
	DBG_ENTER("findDist")
	DBG_LOCAL(t_x1,"x1")
	DBG_LOCAL(t_y1,"y1")
	DBG_LOCAL(t_x2,"x2")
	DBG_LOCAL(t_y2,"y2")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<156>");
	Float t_xDist=t_x1-t_x2;
	DBG_LOCAL(t_xDist,"xDist")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<157>");
	Float t_yDist=t_y1-t_y2;
	DBG_LOCAL(t_yDist,"yDist")
	DBG_INFO("F:/Final Game/GameModules/BasicLibrary.monkey<158>");
	int t_=int((Float)sqrt(t_xDist*t_xDist+t_yDist*t_yDist));
	return t_;
}
int bb_audio_PlaySound(c_Sound* t_sound,int t_channel,int t_flags){
	DBG_ENTER("PlaySound")
	DBG_LOCAL(t_sound,"sound")
	DBG_LOCAL(t_channel,"channel")
	DBG_LOCAL(t_flags,"flags")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/audio.monkey<53>");
	if(((t_sound)!=0) && ((t_sound->m_sample)!=0)){
		DBG_BLOCK();
		bb_audio_device->PlaySample(t_sound->m_sample,t_channel,t_flags);
	}
	return 0;
}
c_Item::c_Item(){
	m_id=0;
	m_state=String();
	m_guiImage=0;
}
c_Item* c_Item::m_new(Float t__x,Float t__y,int t__id){
	DBG_ENTER("Item.new")
	c_Item *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__x,"_x")
	DBG_LOCAL(t__y,"_y")
	DBG_LOCAL(t__id,"_id")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<31>");
	c_GameObject::m_new2();
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<32>");
	m_x=t__x;
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<33>");
	m_y=t__y;
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<34>");
	m_id=t__id;
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<35>");
	m_state=String(L"Dropped",7);
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<36>");
	gc_assign(m_guiImage,bb_graphics_LoadImage(String(L"gui/items/",10)+String(m_id)+String(L".png",4),1,c_Image::m_DefaultFlags));
	return this;
}
c_Item* c_Item::m_new2(){
	DBG_ENTER("Item.new")
	c_Item *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<25>");
	c_GameObject::m_new2();
	return this;
}
int c_Item::p_PickUpItem(){
	DBG_ENTER("Item.PickUpItem")
	c_Item *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<41>");
	m_state=String(L"Equipped",8);
	return 0;
}
int c_Item::p_DrawItem2(int t_pos){
	DBG_ENTER("Item.DrawItem")
	c_Item *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_pos,"pos")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<45>");
	if(m_state==String(L"Dropped",7)){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<47>");
		bb_graphics_DrawImage(m_guiImage,m_x,m_y,0);
	}
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<49>");
	if(m_state==String(L"Equipped",8)){
		DBG_BLOCK();
		DBG_INFO("F:/Final Game/GameModules/Items.monkey<50>");
		bb_graphics_DrawImage(m_guiImage,Float(548+48*t_pos),FLOAT(442.0),0);
	}
	return 0;
}
void c_Item::mark(){
	c_GameObject::mark();
	gc_mark_q(m_guiImage);
}
String c_Item::debug(){
	String t="(Item)\n";
	t=c_GameObject::debug()+t;
	t+=dbg_decl("id",&m_id);
	t+=dbg_decl("state",&m_state);
	t+=dbg_decl("guiImage",&m_guiImage);
	return t;
}
c_HealthPotion::c_HealthPotion(){
}
c_HealthPotion* c_HealthPotion::m_new(Float t__x,Float t__y){
	DBG_ENTER("HealthPotion.new")
	c_HealthPotion *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t__x,"_x")
	DBG_LOCAL(t__y,"_y")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<72>");
	c_Item::m_new(t__x,t__y,2);
	return this;
}
c_HealthPotion* c_HealthPotion::m_new2(){
	DBG_ENTER("HealthPotion.new")
	c_HealthPotion *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<69>");
	c_Item::m_new2();
	return this;
}
int c_HealthPotion::p_Use(c_Player* t_player){
	DBG_ENTER("HealthPotion.Use")
	c_HealthPotion *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_player,"player")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<76>");
	t_player->p_Heal();
	return 0;
}
int c_HealthPotion::p_PickUp(){
	DBG_ENTER("HealthPotion.PickUp")
	c_HealthPotion *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<80>");
	c_Item::p_PickUpItem();
	return 0;
}
int c_HealthPotion::p_Draw2(){
	DBG_ENTER("HealthPotion.Draw")
	c_HealthPotion *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<88>");
	c_Item::p_DrawItem2(0);
	return 0;
}
int c_HealthPotion::p_Draw3(int t_pos){
	DBG_ENTER("HealthPotion.Draw")
	c_HealthPotion *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_pos,"pos")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<92>");
	c_Item::p_DrawItem2(t_pos);
	return 0;
}
Float c_HealthPotion::p_GetX(){
	DBG_ENTER("HealthPotion.GetX")
	c_HealthPotion *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<96>");
	return m_x;
}
Float c_HealthPotion::p_GetY(){
	DBG_ENTER("HealthPotion.GetY")
	c_HealthPotion *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("F:/Final Game/GameModules/Items.monkey<100>");
	return m_y;
}
void c_HealthPotion::mark(){
	c_Item::mark();
}
String c_HealthPotion::debug(){
	String t="(HealthPotion)\n";
	t=c_Item::debug()+t;
	return t;
}
int bb_input_KeyDown(int t_key){
	DBG_ENTER("KeyDown")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/input.monkey<40>");
	int t_=((bb_input_device->p_KeyDown(t_key))?1:0);
	return t_;
}
c_Enumerator3::c_Enumerator3(){
	m__list=0;
	m__curr=0;
}
c_Enumerator3* c_Enumerator3::m_new(c_List* t_list){
	DBG_ENTER("Enumerator.new")
	c_Enumerator3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<326>");
	gc_assign(m__list,t_list);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<327>");
	gc_assign(m__curr,t_list->m__head->m__succ);
	return this;
}
c_Enumerator3* c_Enumerator3::m_new2(){
	DBG_ENTER("Enumerator.new")
	c_Enumerator3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<323>");
	return this;
}
bool c_Enumerator3::p_HasNext(){
	DBG_ENTER("Enumerator.HasNext")
	c_Enumerator3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<331>");
	while(m__curr->m__succ->m__pred!=m__curr){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<332>");
		gc_assign(m__curr,m__curr->m__succ);
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<334>");
	bool t_=m__curr!=m__list->m__head;
	return t_;
}
c_Interactables* c_Enumerator3::p_NextObject(){
	DBG_ENTER("Enumerator.NextObject")
	c_Enumerator3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<338>");
	c_Interactables* t_data=m__curr->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<339>");
	gc_assign(m__curr,m__curr->m__succ);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<340>");
	return t_data;
}
void c_Enumerator3::mark(){
	Object::mark();
	gc_mark_q(m__list);
	gc_mark_q(m__curr);
}
String c_Enumerator3::debug(){
	String t="(Enumerator)\n";
	t+=dbg_decl("_list",&m__list);
	t+=dbg_decl("_curr",&m__curr);
	return t;
}
c_Enumerator4::c_Enumerator4(){
	m__list=0;
	m__curr=0;
}
c_Enumerator4* c_Enumerator4::m_new(c_List2* t_list){
	DBG_ENTER("Enumerator.new")
	c_Enumerator4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<326>");
	gc_assign(m__list,t_list);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<327>");
	gc_assign(m__curr,t_list->m__head->m__succ);
	return this;
}
c_Enumerator4* c_Enumerator4::m_new2(){
	DBG_ENTER("Enumerator.new")
	c_Enumerator4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<323>");
	return this;
}
bool c_Enumerator4::p_HasNext(){
	DBG_ENTER("Enumerator.HasNext")
	c_Enumerator4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<331>");
	while(m__curr->m__succ->m__pred!=m__curr){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<332>");
		gc_assign(m__curr,m__curr->m__succ);
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<334>");
	bool t_=m__curr!=m__list->m__head;
	return t_;
}
c_WeaponInterface* c_Enumerator4::p_NextObject(){
	DBG_ENTER("Enumerator.NextObject")
	c_Enumerator4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<338>");
	c_WeaponInterface* t_data=m__curr->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<339>");
	gc_assign(m__curr,m__curr->m__succ);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/monkey/list.monkey<340>");
	return t_data;
}
void c_Enumerator4::mark(){
	Object::mark();
	gc_mark_q(m__list);
	gc_mark_q(m__curr);
}
String c_Enumerator4::debug(){
	String t="(Enumerator)\n";
	t+=dbg_decl("_list",&m__list);
	t+=dbg_decl("_curr",&m__curr);
	return t;
}
int bb_graphics_DebugRenderDevice(){
	DBG_ENTER("DebugRenderDevice")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<53>");
	if(!((bb_graphics_renderDevice)!=0)){
		DBG_BLOCK();
		bbError(String(L"Rendering operations can only be performed inside OnRender",58));
	}
	return 0;
}
int bb_graphics_Cls(Float t_r,Float t_g,Float t_b){
	DBG_ENTER("Cls")
	DBG_LOCAL(t_r,"r")
	DBG_LOCAL(t_g,"g")
	DBG_LOCAL(t_b,"b")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<382>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<384>");
	bb_graphics_renderDevice->Cls(t_r,t_g,t_b);
	return 0;
}
int bb_graphics_DrawImage(c_Image* t_image,Float t_x,Float t_y,int t_frame){
	DBG_ENTER("DrawImage")
	DBG_LOCAL(t_image,"image")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_frame,"frame")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<455>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<456>");
	if(t_frame<0 || t_frame>=t_image->m_frames.Length()){
		DBG_BLOCK();
		bbError(String(L"Invalid image frame",19));
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<459>");
	c_Frame* t_f=t_image->m_frames.At(t_frame);
	DBG_LOCAL(t_f,"f")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<461>");
	bb_graphics_context->p_Validate();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<463>");
	if((t_image->m_flags&65536)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<464>");
		bb_graphics_renderDevice->DrawSurface(t_image->m_surface,t_x-t_image->m_tx,t_y-t_image->m_ty);
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<466>");
		bb_graphics_renderDevice->DrawSurface2(t_image->m_surface,t_x-t_image->m_tx,t_y-t_image->m_ty,t_f->m_x,t_f->m_y,t_image->m_width,t_image->m_height);
	}
	return 0;
}
int bb_graphics_PushMatrix(){
	DBG_ENTER("PushMatrix")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<338>");
	int t_sp=bb_graphics_context->m_matrixSp;
	DBG_LOCAL(t_sp,"sp")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<339>");
	bb_graphics_context->m_matrixStack.At(t_sp+0)=bb_graphics_context->m_ix;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<340>");
	bb_graphics_context->m_matrixStack.At(t_sp+1)=bb_graphics_context->m_iy;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<341>");
	bb_graphics_context->m_matrixStack.At(t_sp+2)=bb_graphics_context->m_jx;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<342>");
	bb_graphics_context->m_matrixStack.At(t_sp+3)=bb_graphics_context->m_jy;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<343>");
	bb_graphics_context->m_matrixStack.At(t_sp+4)=bb_graphics_context->m_tx;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<344>");
	bb_graphics_context->m_matrixStack.At(t_sp+5)=bb_graphics_context->m_ty;
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<345>");
	bb_graphics_context->m_matrixSp=t_sp+6;
	return 0;
}
int bb_graphics_Transform(Float t_ix,Float t_iy,Float t_jx,Float t_jy,Float t_tx,Float t_ty){
	DBG_ENTER("Transform")
	DBG_LOCAL(t_ix,"ix")
	DBG_LOCAL(t_iy,"iy")
	DBG_LOCAL(t_jx,"jx")
	DBG_LOCAL(t_jy,"jy")
	DBG_LOCAL(t_tx,"tx")
	DBG_LOCAL(t_ty,"ty")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<359>");
	Float t_ix2=t_ix*bb_graphics_context->m_ix+t_iy*bb_graphics_context->m_jx;
	DBG_LOCAL(t_ix2,"ix2")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<360>");
	Float t_iy2=t_ix*bb_graphics_context->m_iy+t_iy*bb_graphics_context->m_jy;
	DBG_LOCAL(t_iy2,"iy2")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<361>");
	Float t_jx2=t_jx*bb_graphics_context->m_ix+t_jy*bb_graphics_context->m_jx;
	DBG_LOCAL(t_jx2,"jx2")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<362>");
	Float t_jy2=t_jx*bb_graphics_context->m_iy+t_jy*bb_graphics_context->m_jy;
	DBG_LOCAL(t_jy2,"jy2")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<363>");
	Float t_tx2=t_tx*bb_graphics_context->m_ix+t_ty*bb_graphics_context->m_jx+bb_graphics_context->m_tx;
	DBG_LOCAL(t_tx2,"tx2")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<364>");
	Float t_ty2=t_tx*bb_graphics_context->m_iy+t_ty*bb_graphics_context->m_jy+bb_graphics_context->m_ty;
	DBG_LOCAL(t_ty2,"ty2")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<365>");
	bb_graphics_SetMatrix(t_ix2,t_iy2,t_jx2,t_jy2,t_tx2,t_ty2);
	return 0;
}
int bb_graphics_Transform2(Array<Float > t_m){
	DBG_ENTER("Transform")
	DBG_LOCAL(t_m,"m")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<355>");
	bb_graphics_Transform(t_m.At(0),t_m.At(1),t_m.At(2),t_m.At(3),t_m.At(4),t_m.At(5));
	return 0;
}
int bb_graphics_Translate(Float t_x,Float t_y){
	DBG_ENTER("Translate")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<369>");
	bb_graphics_Transform(FLOAT(1.0),FLOAT(0.0),FLOAT(0.0),FLOAT(1.0),t_x,t_y);
	return 0;
}
int bb_graphics_Rotate(Float t_angle){
	DBG_ENTER("Rotate")
	DBG_LOCAL(t_angle,"angle")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<377>");
	bb_graphics_Transform((Float)cos((t_angle)*D2R),-(Float)sin((t_angle)*D2R),(Float)sin((t_angle)*D2R),(Float)cos((t_angle)*D2R),FLOAT(0.0),FLOAT(0.0));
	return 0;
}
int bb_graphics_Scale(Float t_x,Float t_y){
	DBG_ENTER("Scale")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<373>");
	bb_graphics_Transform(t_x,FLOAT(0.0),FLOAT(0.0),t_y,FLOAT(0.0),FLOAT(0.0));
	return 0;
}
int bb_graphics_PopMatrix(){
	DBG_ENTER("PopMatrix")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<349>");
	int t_sp=bb_graphics_context->m_matrixSp-6;
	DBG_LOCAL(t_sp,"sp")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<350>");
	bb_graphics_SetMatrix(bb_graphics_context->m_matrixStack.At(t_sp+0),bb_graphics_context->m_matrixStack.At(t_sp+1),bb_graphics_context->m_matrixStack.At(t_sp+2),bb_graphics_context->m_matrixStack.At(t_sp+3),bb_graphics_context->m_matrixStack.At(t_sp+4),bb_graphics_context->m_matrixStack.At(t_sp+5));
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<351>");
	bb_graphics_context->m_matrixSp=t_sp;
	return 0;
}
int bb_graphics_DrawImage2(c_Image* t_image,Float t_x,Float t_y,Float t_rotation,Float t_scaleX,Float t_scaleY,int t_frame){
	DBG_ENTER("DrawImage")
	DBG_LOCAL(t_image,"image")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_rotation,"rotation")
	DBG_LOCAL(t_scaleX,"scaleX")
	DBG_LOCAL(t_scaleY,"scaleY")
	DBG_LOCAL(t_frame,"frame")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<473>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<474>");
	if(t_frame<0 || t_frame>=t_image->m_frames.Length()){
		DBG_BLOCK();
		bbError(String(L"Invalid image frame",19));
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<477>");
	c_Frame* t_f=t_image->m_frames.At(t_frame);
	DBG_LOCAL(t_f,"f")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<479>");
	bb_graphics_PushMatrix();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<481>");
	bb_graphics_Translate(t_x,t_y);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<482>");
	bb_graphics_Rotate(t_rotation);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<483>");
	bb_graphics_Scale(t_scaleX,t_scaleY);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<485>");
	bb_graphics_Translate(-t_image->m_tx,-t_image->m_ty);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<487>");
	bb_graphics_context->p_Validate();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<489>");
	if((t_image->m_flags&65536)!=0){
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<490>");
		bb_graphics_renderDevice->DrawSurface(t_image->m_surface,FLOAT(0.0),FLOAT(0.0));
	}else{
		DBG_BLOCK();
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<492>");
		bb_graphics_renderDevice->DrawSurface2(t_image->m_surface,FLOAT(0.0),FLOAT(0.0),t_f->m_x,t_f->m_y,t_image->m_width,t_image->m_height);
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<495>");
	bb_graphics_PopMatrix();
	return 0;
}
int bb_graphics_DrawRect(Float t_x,Float t_y,Float t_w,Float t_h){
	DBG_ENTER("DrawRect")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_w,"w")
	DBG_LOCAL(t_h,"h")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<397>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<399>");
	bb_graphics_context->p_Validate();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<400>");
	bb_graphics_renderDevice->DrawRect(t_x,t_y,t_w,t_h);
	return 0;
}
int bb_LevelGeneration_DrawRoom(String t_path,int t_x,int t_y,int t_mapPSize,int t_mapXOffset){
	DBG_ENTER("DrawRoom")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_mapPSize,"mapPSize")
	DBG_LOCAL(t_mapXOffset,"mapXOffset")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<876>");
	c_Image* t_Room=0;
	DBG_LOCAL(t_Room,"Room")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<877>");
	int t_ImageSize=25;
	DBG_LOCAL(t_ImageSize,"ImageSize")
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<878>");
	t_Room=bb_graphics_LoadImage2(t_path,t_ImageSize,t_ImageSize,1,c_Image::m_DefaultFlags);
	DBG_INFO("F:/Final Game/GameModules/LevelGeneration.monkey<879>");
	bb_graphics_DrawImage(t_Room,Float(t_x*t_mapPSize+t_mapXOffset),Float(t_y*t_mapPSize),0);
	return 0;
}
int bb_graphics_DrawText(String t_text,Float t_x,Float t_y,Float t_xalign,Float t_yalign){
	DBG_ENTER("DrawText")
	DBG_LOCAL(t_text,"text")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_xalign,"xalign")
	DBG_LOCAL(t_yalign,"yalign")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<580>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<582>");
	if(!((bb_graphics_context->m_font)!=0)){
		DBG_BLOCK();
		return 0;
	}
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<584>");
	int t_w=bb_graphics_context->m_font->p_Width();
	DBG_LOCAL(t_w,"w")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<585>");
	int t_h=bb_graphics_context->m_font->p_Height();
	DBG_LOCAL(t_h,"h")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<587>");
	t_x-=(Float)floor(Float(t_w*t_text.Length())*t_xalign);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<588>");
	t_y-=(Float)floor(Float(t_h)*t_yalign);
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<590>");
	for(int t_i=0;t_i<t_text.Length();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<591>");
		int t_ch=(int)t_text.At(t_i)-bb_graphics_context->m_firstChar;
		DBG_LOCAL(t_ch,"ch")
		DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<592>");
		if(t_ch>=0 && t_ch<bb_graphics_context->m_font->p_Frames()){
			DBG_BLOCK();
			DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/graphics.monkey<593>");
			bb_graphics_DrawImage(bb_graphics_context->m_font,t_x+Float(t_i*t_w),t_y,t_ch);
		}
	}
	return 0;
}
Float bb_input_MouseX(){
	DBG_ENTER("MouseX")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/input.monkey<58>");
	Float t_=bb_input_device->p_MouseX();
	return t_;
}
Float bb_input_MouseY(){
	DBG_ENTER("MouseY")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/input.monkey<62>");
	Float t_=bb_input_device->p_MouseY();
	return t_;
}
int bb_input_MouseHit(int t_button){
	DBG_ENTER("MouseHit")
	DBG_LOCAL(t_button,"button")
	DBG_INFO("C:/Users/camjr/Documents/MonkeyX/JungleMonkey78b/modules/mojo/input.monkey<70>");
	int t_=bb_input_device->p_KeyHit(1+t_button);
	return t_;
}
int bbInit(){
	GC_CTOR
	bb_app__app=0;
	DBG_GLOBAL("_app",&bb_app__app);
	bb_app__delegate=0;
	DBG_GLOBAL("_delegate",&bb_app__delegate);
	bb_app__game=BBGame::Game();
	DBG_GLOBAL("_game",&bb_app__game);
	bb_Ragnarok_game=0;
	DBG_GLOBAL("game",&bb_Ragnarok_game);
	bb_graphics_device=0;
	DBG_GLOBAL("device",&bb_graphics_device);
	bb_graphics_context=(new c_GraphicsContext)->m_new();
	DBG_GLOBAL("context",&bb_graphics_context);
	c_Image::m_DefaultFlags=0;
	DBG_GLOBAL("DefaultFlags",&c_Image::m_DefaultFlags);
	bb_audio_device=0;
	DBG_GLOBAL("device",&bb_audio_device);
	bb_input_device=0;
	DBG_GLOBAL("device",&bb_input_device);
	bb_app__devWidth=0;
	DBG_GLOBAL("_devWidth",&bb_app__devWidth);
	bb_app__devHeight=0;
	DBG_GLOBAL("_devHeight",&bb_app__devHeight);
	bb_app__displayModes=Array<c_DisplayMode* >();
	DBG_GLOBAL("_displayModes",&bb_app__displayModes);
	bb_app__desktopMode=0;
	DBG_GLOBAL("_desktopMode",&bb_app__desktopMode);
	bb_graphics_renderDevice=0;
	DBG_GLOBAL("renderDevice",&bb_graphics_renderDevice);
	bb_app__updateRate=0;
	DBG_GLOBAL("_updateRate",&bb_app__updateRate);
	bb_random_Seed=1234;
	DBG_GLOBAL("Seed",&bb_random_Seed);
	return 0;
}
void gc_mark(){
	gc_mark_q(bb_app__app);
	gc_mark_q(bb_app__delegate);
	gc_mark_q(bb_Ragnarok_game);
	gc_mark_q(bb_graphics_device);
	gc_mark_q(bb_graphics_context);
	gc_mark_q(bb_audio_device);
	gc_mark_q(bb_input_device);
	gc_mark_q(bb_app__displayModes);
	gc_mark_q(bb_app__desktopMode);
	gc_mark_q(bb_graphics_renderDevice);
}
//${TRANSCODE_END}

int main( int argc,const char *argv[] ){

	BBMonkeyGame::Main( argc,argv );
}
