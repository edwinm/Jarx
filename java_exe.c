// Java - exe
// Execute Java software as normal Windows executables
// Copyright 2004 Edwin Martin

#include <jni.h>
#include <windows.h>
#include <direct.h>
#include "resource.h"

jint (__stdcall *JNI_CreateJavaVM_X)( JavaVM**, void**, JavaVMInitArgs* );
void (__stdcall *JNI_GetDefaultJavaVMInitArgs_X)( JDK1_1InitArgs* );
int getRegParam( char *regPath, char *regItem, char *strBuffer, int strBufLen );
HINSTANCE getJVM( int * );
void errorMessage( int );
char *skipWhitespace( char *s, int offset );
int execute( char *cmd );
void showVersion( jint version );
void strReplace( char *s, char org, char dest );
void truncateNewline( char *line );
int execJVM( HINSTANCE hVM, HINSTANCE hInstance, char *jarfile, char *classpath, char *classname, char *arg, int version, int minversion );
void getCmdParam( char *cmd, char *cleanCmd, int maxlen );
char *_getcwd( char *buffer, int maxlen );
int getConfigParam( char *dest, char *in, char *match, int size );
int parseVersion( char *s );
char *getManifest( JNIEnv *env, char *jarname );
int getClassName( char *manifest, char *classname, int size );
int readConfig( char *cmd, char *jarfile, char *classpath, char *classname, char *versionmin );
BOOL CALLBACK aboutCallback( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam );
void showit( char *name, long value );

#define JREPATH			"SOFTWARE\\JavaSoft\\Java Runtime Environment"
#define JARX_JARFILE	"Jar-file:"
#define JARX_CLASSPATH	"Classpath:"
#define JARX_VERSIONMIN "Version-min:"
#define JAVADOWNLOAD	"http://www.java.com/"
#define JARXWEBSITE		"http://www.bitstorm.org/jarx/"
#define MAINCLASS		"Main-Class:"
#define LINEMAXLEN		1000
#define DEFAULTJARX		"default.jarx"

#define JVM_OK 0
#define JVM_CLASSERR 1
#define JVM_VERSIONTOOSMALL 2
#define JVM_PROBLEM 3

//#define SHOWDEBUG 1

HINSTANCE hInst;

int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nShowCmd ) {
	HINSTANCE hVM;
	int result;
	char line[LINEMAXLEN];
	char jarfile[LINEMAXLEN];
	char classpath[LINEMAXLEN];
	char classname[LINEMAXLEN];
	char errorStr[LINEMAXLEN];
	char downloadStr[LINEMAXLEN];
	char cmd[2*LINEMAXLEN];
	char versionmin[30];
	int versmin = 0;
	int version;
	char *p;
	char *pExt;
	int error = 0;
	char *arg = NULL;

	*jarfile = '\0';
	*classpath = '\0';
	*classname = '\0';
	*versionmin = '\0';

	hInst = hInstance;

	// Possible situations:
	// 1) Jarx is opened because a .jar is double clicked:
	//		Read manifest and execute
	// 2) Jarx is opened because a .jarx is double clicked:
	//		Read jarx and execute
	// 2) Jarx is opened because some other file is double clicked
	//		Try to read a default.jarx in the app map
	// Else: show Jarx dialog box

	// Remove dblquotes from cmdline
	getCmdParam( lpCmdLine, line, sizeof( line ) - 1 );
	GetLongPathName( line, cmd, sizeof( cmd ) - 1 );

	// Find extension
	pExt = strrchr( cmd, '.' );
	if ( pExt != NULL && stricmp( pExt, ".jarx" ) == 0 ) {
		// it's .jarx - read and parse .jarx file
		readConfig( cmd, jarfile, classpath, classname, versionmin );
	} else if ( pExt != NULL && stricmp( pExt, ".jar" ) == 0 ) {
		// it's .jar
		// Only use filename (without path)
		// p = strrchr( cmd, '\\' );
		//if ( p != NULL )
		//	strcpy( classpath, p+1 );
		//else
		strcpy( jarfile, cmd );
	} else {
		// It's not a .jar or .jarx - try to get the default.jarx from the application map
		getCmdParam( GetCommandLine(), cmd, sizeof(cmd) );
		p = strrchr( cmd, '\\' );
		if ( p != NULL ) {
			*p = '\0';
			_chdir( cmd );
		}
		readConfig( DEFAULTJARX, jarfile, classpath, classname, versionmin );
		getCmdParam( lpCmdLine, cmd, sizeof(cmd) );
		if ( strlen( cmd ) > 0 )
			arg = cmd;
	}

	// Still no jarfile found? Show JARX dialog
	if ( *jarfile == '\0' ) {
		DialogBox( hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, aboutCallback );
		return 0;
	}

	// No classpath? -> use jarfile as classpath
	if ( *classpath == '\0' )
		strcpy( classpath, jarfile );

	// Versionmin set?
	if ( *versionmin != '\0' )
		versmin = parseVersion( versionmin );
	else
		versmin = 0;

	// try to load a JVM
	hVM = getJVM( &version );

	if ( hVM == NULL ) {
		error = IDS_NOTINSTALLED;
	} else {
		// execute class in JVM
		result = execJVM( hVM, hInstance, jarfile, classpath, classname, arg, version, versmin );
		switch( result ) {
			case JVM_PROBLEM:
				error = IDS_CANTCREATEJVM;
				break;
			case JVM_VERSIONTOOSMALL:
				error = IDS_JAVATOOOLD;
				break;
			case JVM_CLASSERR:
				// Probably MS JVM
				// Just execute jview
				if ( *classname != '\0' ) {
					strReplace( classname, '/', '.' );
					sprintf( cmd, "jview /cp \"%s\" %s", classpath, classname );
					result = execute( cmd );
					if ( !result )
						error = IDS_NOJVIEW;
					break;

				} else
					error = IDS_NOJVIEW;
		}
	}

	// Error occured
	if ( error != 0 ) {
		LoadString( hInst, error, errorStr, sizeof( errorStr ) - 1 );
		LoadString( hInst, IDS_DOWNLOAD, downloadStr, sizeof( downloadStr ) - 1 );
		sprintf( line, downloadStr, errorStr );
		result = MessageBox( NULL, line, "JARX", MB_OKCANCEL | MB_ICONEXCLAMATION );
		if ( result == IDOK )
			ShellExecute(NULL, "open", JAVADOWNLOAD, NULL, NULL, SW_SHOWNORMAL);
		return 1;
	}
	return 0;
}

int execJVM( HINSTANCE hVM, HINSTANCE hInstance, char *jarfile, char *classpath, char *classname, char *arg, int version, int minversion ) {
    JNIEnv *env;
    JavaVM *jvm;
    jint res;
    jmethodID mid;
    jclass cls;
	jclass stringClass;
    jobjectArray args = NULL;
	jstring jstr;
	int ret = JVM_OK;
	char *manifest;
	HWND startingJava;

	// "Starting Java" dialog box
	startingJava = CreateDialog( hInstance, MAKEINTRESOURCE(IDD_DIALOG2), NULL, aboutCallback );
	ShowWindow(startingJava, SW_SHOW); 

	(*(FARPROC*)&JNI_CreateJavaVM_X = GetProcAddress( hVM, "JNI_CreateJavaVM" ));

	if ( JNI_CreateJavaVM_X == NULL ) // Happens on Win98
		return JVM_CLASSERR;

	if ( version == 1 ) {
		JDK1_1InitArgs vm_args;

		(*(FARPROC*)&JNI_GetDefaultJavaVMInitArgs_X = GetProcAddress( hVM, "JNI_GetDefaultJavaVMInitArgs" ));
		vm_args.version = JNI_VERSION_1_1;
		JNI_GetDefaultJavaVMInitArgs_X( &vm_args );
		vm_args.classpath = classpath;
		res = JNI_CreateJavaVM_X( &jvm, (void**)&env, (JavaVMInitArgs*) &vm_args );
	} else {
		JavaVMInitArgs vm_args;
		JavaVMOption options[1];
		char jvm_opt[LINEMAXLEN+100];
		sprintf( jvm_opt, "-Djava.class.path=%s", classpath );
		options[0].optionString =  jvm_opt;
		vm_args.version = JNI_VERSION_1_2;
		vm_args.options = options;
		vm_args.nOptions = 1;
		vm_args.ignoreUnrecognized = TRUE;
		res = JNI_CreateJavaVM_X( &jvm, (void**)&env, &vm_args );
	}

    if ( res < 0 )
        return JVM_PROBLEM;

	version = (*env)->GetVersion( env );

	if ( version < minversion ) {
		ret = JVM_VERSIONTOOSMALL;
        goto destroy;
    }

#ifdef SHOWDEBUG
	showVersion( version );
#endif

	if ( *classname == '\0' ) {
		manifest = getManifest( env, jarfile );
		getClassName( manifest, classname, LINEMAXLEN );
	}

	strReplace( classname, '.', '/' );

    cls = (*env)->FindClass( env, classname );

    if ( cls == 0 ) {
		ret = JVM_CLASSERR;
        goto destroy;
    }

	jstr = (*env)->NewStringUTF( env, arg?arg:"" );
	stringClass = (*env)->FindClass( env, "java/lang/String" );
	args = (*env)->NewObjectArray( env, arg?1:0, stringClass, jstr );

	mid = (*env)->GetStaticMethodID( env, cls, "main", "([Ljava/lang/String;)V" );
	if ( mid == 0 ) {
		ret = JVM_CLASSERR;
		goto destroy;
	}

	DestroyWindow(startingJava);
	
	(*env)->CallStaticVoidMethod( env, cls, mid, args );

	destroy:
	//if (env->ExceptionOccurred()) {
	//	env->ExceptionDescribe();
	//}

	(*jvm)->DestroyJavaVM(jvm);

	return ret;

}

// Find a JVM
HINSTANCE getJVM( int *version ) {
	int result;
	HINSTANCE hVM;
	char currentJREPath[1000];
	char buffer[500];

	result = getRegParam( JREPATH, "CurrentVersion", buffer, sizeof( buffer ) );

	if ( result ) {
		sprintf( currentJREPath, "%s\\%s", JREPATH, buffer );

		result = getRegParam( currentJREPath, "RuntimeLib", buffer, sizeof( buffer ) );

		if ( result ) {
			hVM = LoadLibrary( buffer );
			// test hVM = NULL;

			if ( hVM != NULL )
				return hVM;
		}
	}

	*version = 2;
	// Load Sun J2SE
	hVM = LoadLibrary( "jvm.dll" );

	if ( hVM != NULL )
		return hVM;

	*version = 1;
	// Load Sun JDK 1.1
	hVM = LoadLibrary( "javai.dll" );
	
	if ( hVM != NULL )
		return hVM;

	// Load MS JVM
	hVM = LoadLibrary( "msjava.dll" );

	return hVM;
}

// Shows an error message
void errorMessage( int idc ) {
	char errorStr[LINEMAXLEN];

	LoadString( hInst, idc, errorStr, sizeof( errorStr ) );
	MessageBox( NULL, errorStr, "Jarx", MB_ICONEXCLAMATION );
}

// Gets a registry value from the HKLM-space
int getRegParam( char *regPath, char *regItem, char *strBuffer, int strBufLen ) {
	DWORD iValue = 0;
	LONG lSuccess;
	HKEY hKey;
	char szValue[100], szData[500];
	DWORD dwType, cchValue, cbData;   

	lSuccess = RegOpenKeyEx( HKEY_LOCAL_MACHINE, regPath, 0, KEY_QUERY_VALUE, &hKey );   

	if ( lSuccess == ERROR_SUCCESS ) {
		while( TRUE )   {     
			cchValue = sizeof( szValue );     
			cbData = sizeof( szData );
			lSuccess = RegEnumValue( hKey, iValue++, szValue, &cchValue, NULL, &dwType, szData, &cbData );     
			if (lSuccess == ERROR_NO_MORE_ITEMS)       
				break;     

			if ( strcmp( szValue, regItem ) == 0 ) {
				strncpy( strBuffer, szData, strBufLen );
				return TRUE;
			}
		}
	}
	return FALSE;
}

// Return pointer to the first non-whitespace char in string
char *skipWhitespace( char *s, int offset ) {
	s += offset;
	while( isspace( *s ) && *s != '\0' )
		s++;
	return s;
}

// Execute program
int execute( char *cmd ) {
	STARTUPINFO stinfo;
	PROCESS_INFORMATION pinfo;
	int result;

	GetStartupInfo( &stinfo );
	stinfo.dwFlags = STARTF_USESHOWWINDOW;
	result = CreateProcess( NULL, cmd, NULL, NULL, FALSE, NORMAL_PRIORITY_CLASS | DETACHED_PROCESS, NULL, NULL, &stinfo, &pinfo );
	return result;
}

#ifdef SHOWDEBUG
// Show Java version
void showVersion( jint version ) {
	char versionStr[20];
	int version_major, version_minor;

	version_major = version >> 16;
	version_minor = version & 0x0000FFFF;

	sprintf( versionStr, "version: %d.%d", version_major, version_minor );
	MessageBox( NULL, versionStr, "Jarx", 0 );
}
#endif

// Replace chars in a string with other chars
void strReplace( char *s, char org, char dest ) {
	while ( *s ) {
		if ( *s == org )
			*s = dest;
		s++;
	}
}

// Truncate a string at \n or \r
void truncateNewline( char *s ) {
	while ( *s ) {
		if ( *s == '\n' || *s == '\r' ) {
			*s = '\0';
			break;
		}
		s++;
	}
}

// Copies the part between the first two "'s or just the whole string
// command line -> command line
// "command line" -> command line
void getCmdParam( char *cmd, char *cleanCmd, int maxlen ) {
	char *walk;

	if ( *cmd == '"' ) {
		walk = cmd + 1;
		while ( *walk && *walk != '"' && --maxlen ) {
			*(cleanCmd++) = *(walk++);
		}
		*cleanCmd = '\0';
	} else {
		strncpy( cleanCmd, cmd, maxlen );
	}
}

int getConfigParam( char *dest, char *in, char *match, int size ) {
	if ( strnicmp( in, match, size -1 ) == 0 ) {
		strcpy( dest, skipWhitespace( in, size ) );
		return TRUE;
	} else
		return FALSE;
}

// Parse version in string to Java-format
int parseVersion( char *s ) {
	int major = 0, minor = 0;
	int level = 0;

	while ( *s ) {
		if ( isdigit( *s ) ) {
			if ( level == 0 )
				major = major * 10 + *s - '0';
			else
				minor = minor * 10 + *s - '0';
		} else if ( *s == '.' ) {
			level++;
			if ( level == 2 )
				break;
		} else
			// Parse error
			return 0;
		s++;
	}
	return (major << 16) | minor;
}

#define MANIFESTMAXLEN 2000

// Use the JVM to get zipfile-entry
char *getManifest( JNIEnv *env, char *jarname ) {
    jclass cls;
    jmethodID mid;
    jobject zip, zen, ist, bar;
    jstring str;
	jint bytesRead;
	char *manifest;

	// zip = new ZipFile( jarname );
	cls = (*env)->FindClass(env, "java/util/zip/ZipFile");
    mid = (*env)->GetMethodID(env, cls, "<init>", "(Ljava/lang/String;)V");
    str = (*env)->NewStringUTF(env, jarname);
    zip = (*env)->NewObject(env, cls, mid, str);

	if ( zip == NULL )
		return "";

	// zen = zip.getEntry( "META-INF/MANIFEST.MF" );
	str = (*env)->NewStringUTF(env, "META-INF/MANIFEST.MF");
    mid = (*env)->GetMethodID(env, cls, "getEntry", "(Ljava/lang/String;)Ljava/util/zip/ZipEntry;");
    zen = (*env)->CallObjectMethod(env, zip, mid, str);

	// ist = zip.getInputStream( zen );
    mid = (*env)->GetMethodID(env, cls, "getInputStream", "(Ljava/util/zip/ZipEntry;)Ljava/io/InputStream;");
    ist = (*env)->CallObjectMethod(env, zip, mid, zen);

	cls = (*env)->FindClass(env, "java/io/InputStream");

	// bar = new byte[MANIFESTMAXLEN];
	bar = (*env)->NewByteArray(env, MANIFESTMAXLEN);

	// bytesRead = ist.read( bar );
    mid = (*env)->GetMethodID(env, cls, "read", "([BII)I");
    bytesRead = (int) (*env)->CallIntMethod(env, ist, mid, bar, 0, MANIFESTMAXLEN);

	cls = (*env)->FindClass(env, "java/lang/String");
    mid = (*env)->GetMethodID(env, cls, "<init>", "([BII)V");
    str = (*env)->NewObject(env, cls, mid, bar, 0, bytesRead);
	
	manifest = (char *)(*env)->GetStringUTFChars(env, str, 0);

	return manifest;
}

#ifdef SHOWDEBUG
void showit( char *name, long value ) {
	char line[1000];
	sprintf( line, "%s: %d", name, value );
	MessageBox( NULL, line, "Jarx", 0 );
}
#endif

// Read classname from manifest
int getClassName( char *manifest, char *classname, int size ) {
	char *cptr;

	cptr = strstr( manifest, MAINCLASS );

	if ( cptr == NULL )
		return 1;

	cptr = skipWhitespace( cptr, sizeof( MAINCLASS ) );

	truncateNewline( cptr );

	strncpy( classname, cptr, size );

	return 0;
}

// Parse jarx config file
int readConfig( char *cmd, char *jarfile, char *classpath, char *classname, char *versionmin ) {
	FILE *fh;
	char line[LINEMAXLEN];

	fh = fopen( cmd, "r" );
	if ( fh == NULL ) {
		return 1;
	} else {
		while ( fgets( line, LINEMAXLEN, fh ) ) {
			truncateNewline( line );
				 if ( getConfigParam( jarfile, line, JARX_JARFILE, sizeof( JARX_JARFILE ) - 1 ) )
					;
			else if ( getConfigParam( classpath, line, JARX_CLASSPATH, sizeof( JARX_CLASSPATH ) - 1 ) )
					;
			else if ( getConfigParam( classname, line, MAINCLASS, sizeof( MAINCLASS ) - 1 ) )
					;
			else      getConfigParam( versionmin, line, JARX_VERSIONMIN, sizeof( JARX_VERSIONMIN ) - 1 );
		}
		fclose( fh );
	}
	return 0;
}

// Callback function for About dialog box
BOOL CALLBACK aboutCallback( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam ) {
	char about[LINEMAXLEN];

	switch ( message ) {
		case WM_INITDIALOG:
			LoadString( hInst, IDS_ABOUT, about, sizeof( about ) - 1 );
			SetDlgItemText( hDlg, IDC_INFO, about );
			return TRUE;
		case WM_COMMAND:
			switch ( LOWORD( wParam ) ) {
				case IDOK:
					EndDialog( hDlg, 0 );
					return TRUE;
					break;
				case IDC_WEBSITE:
					ShellExecute(NULL, "open", JARXWEBSITE, NULL, NULL, SW_SHOWNORMAL);
					return TRUE;
					break;
			}
			break;
		case WM_CLOSE:
			EndDialog( hDlg, 0 );
			return TRUE;
			break;
	}
	return FALSE;
}
