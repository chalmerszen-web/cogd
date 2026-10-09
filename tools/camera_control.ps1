param([string]$Name='Insta360 Link',[switch]$ResetPose,[ValidateRange(-30,30)][int]$Tilt)
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
[ComImport,Guid("29840822-5B84-11D0-BD3B-00A0C911CE86"),InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IDevEnum { [PreserveSig] int CreateClassEnumerator(ref Guid category,out IEnumMoniker list,int flags); }
[ComImport,Guid("55272A00-42CB-11CE-8135-00AA004BB851"),InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IBag {
 [PreserveSig] int Read([MarshalAs(UnmanagedType.LPWStr)] string name,[MarshalAs(UnmanagedType.Struct)] out object value,IntPtr error);
 [PreserveSig] int Write([MarshalAs(UnmanagedType.LPWStr)] string name,[MarshalAs(UnmanagedType.Struct)] ref object value);
}
[ComImport,Guid("C6E13370-30AC-11D0-A18C-00A0C9118956"),InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface ICameraControl {
 [PreserveSig] int GetRange(int property,out int min,out int max,out int step,out int def,out int flags);
 [PreserveSig] int Set(int property,int value,int flags);
 [PreserveSig] int Get(int property,out int value,out int flags);
}
public static class CameraControlProbe {
 public static string[] Run(string wanted,bool reset,bool setTilt,int tilt) {
  var lines=new List<string>();
  object deviceEnum=Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("62BE5D10-60EB-11D0-BD3B-00A0C911CE86")));
  IEnumMoniker list=null;
  try {
   Guid category=new Guid("860BB310-5D01-11D0-BD3B-00A0C911CE86");
   if(((IDevEnum)deviceEnum).CreateClassEnumerator(ref category,out list,0)!=0) throw new Exception("No video devices");
   var one=new IMoniker[1];
   while(list.Next(1,one,IntPtr.Zero)==0) {
    object bag=null,filter=null;
    try {
     Guid bagId=new Guid("55272A00-42CB-11CE-8135-00AA004BB851");
     one[0].BindToStorage(null,null,ref bagId,out bag);object name;
     Marshal.ThrowExceptionForHR(((IBag)bag).Read("FriendlyName",out name,IntPtr.Zero));
     if((string)name!=wanted) continue;
     Guid filterId=new Guid("56A86895-0AD4-11CE-B03A-0020AF0BA770");
     one[0].BindToObject(null,null,ref filterId,out filter);
     var camera=(ICameraControl)filter;
     for(int p=0;p<=6;p++) {
      int min,max,step,def,caps,current,flags;
      int range=camera.GetRange(p,out min,out max,out step,out def,out caps);
      int get=camera.Get(p,out current,out flags);
      lines.Add(String.Format("property={0} range_hr={1:X8} min={2} max={3} step={4} default={5} caps={6} get_hr={7:X8} value={8} flags={9}",p,range,min,max,step,def,caps,get,current,flags));
      if(reset && p<2 && range==0 && (caps&2)!=0) {
       int set=camera.Set(p,def,2);lines.Add(String.Format("reset property={0} target={1} hr={2:X8}",p,def,set));
      }
      if(setTilt && p==1 && range==0 && (caps&2)!=0) {
       if(tilt<min || tilt>max) throw new ArgumentOutOfRangeException("tilt");
       int set=camera.Set(p,tilt,2);lines.Add(String.Format("tilt target={0} hr={1:X8}",tilt,set));
      }
     }
     return lines.ToArray();
    } finally {
     if(filter!=null) Marshal.ReleaseComObject(filter);
     if(bag!=null) Marshal.ReleaseComObject(bag);
     Marshal.ReleaseComObject(one[0]);
    }
   }
   throw new Exception("Requested camera not found");
  } finally { if(list!=null)Marshal.ReleaseComObject(list);Marshal.ReleaseComObject(deviceEnum); }
 }
}
'@
[CameraControlProbe]::Run($Name,$ResetPose.IsPresent,$PSBoundParameters.ContainsKey('Tilt'),$Tilt)
