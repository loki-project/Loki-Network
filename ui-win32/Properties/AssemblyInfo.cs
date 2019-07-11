using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

// General Information about an assembly is controlled through the following
// set of attributes. Change these attribute values to modify the information
// associated with an assembly.
[assembly: AssemblyTitle("LokiNET")]
[assembly: AssemblyDescription("LokiNET end-user UI")]
[assembly: AssemblyConfiguration("")]
[assembly: AssemblyCompany("Loki Project")]
[assembly: AssemblyProduct("LokiNET Launcher")]
[assembly: AssemblyCopyright("Copyright ©2018-2019 Loki Project. All rights reserved. See LICENSE for more details.")]
[assembly: AssemblyTrademark("Loki, Loki Project, LokiNET are ™ & ©2018-2019 Loki Foundation")]
[assembly: AssemblyCulture("")]

// Setting ComVisible to false makes the types in this assembly not visible
// to COM components.  If you need to access a type in this assembly from
// COM, set the ComVisible attribute to true on that type.
[assembly: ComVisible(false)]

// The following GUID is for the ID of the typelib if this project is exposed to COM
[assembly: Guid("1cdee73c-29c5-4781-bd74-1eeac6f75a14")]

// Version information for an assembly consists of the following four values:
//
//      Major Version
//      Minor Version
//      Build Number
//      Revision
//
// You can specify all the values or you can default the Build and Revision Numbers
// by using the '*' as shown below:
// [assembly: AssemblyVersion("1.0.*")]
[assembly: AssemblyVersion("0.4.3")]
[assembly: AssemblyFileVersion("0.4.3")]
#if DEBUG
[assembly: AssemblyInformationalVersion("0.4.3-dev-{chash:8}")]
#else
[assembly: AssemblyInformationalVersion("0.4.3 (RELEASE_CODENAME)")]
#endif