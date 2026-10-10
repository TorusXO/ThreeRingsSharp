import java.io.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import java.util.stream.*;

/** Reads installed game classes reflectively; no game code or assets are bundled. */
public final class KnightAnimationExport {
    static final List<Map<String,Object>> bones = new ArrayList<>();
    static final Map<String,Object> defaults = new LinkedHashMap<>();
    static Object field(Object value, String name) throws Exception {
        for (Class<?> c=value.getClass(); c!=null; c=c.getSuperclass()) {
            try { Field f=c.getDeclaredField(name); f.setAccessible(true); return f.get(value); }
            catch (NoSuchFieldException ignored) { }
        }
        throw new NoSuchFieldException(value.getClass().getName()+"."+name);
    }
    static Object read(Path path) throws Exception {
        Class<?> c=Class.forName("com.threerings.export.BinaryImporter");
        try (InputStream stream=Files.newInputStream(path)) {
            Object reader=c.getConstructor(InputStream.class).newInstance(stream);
            try {
                // Obfuscated installations retain the unique zero-argument Object reader.
                for (Method m:c.getDeclaredMethods())
                    if (Modifier.isPublic(m.getModifiers()) && !Modifier.isStatic(m.getModifiers()) &&
                        m.getParameterCount()==0 && m.getReturnType()==Object.class) return m.invoke(reader);
                throw new IOException("Cannot locate BinaryImporter.readObject in this game version");
            } finally { c.getMethod("close").invoke(reader); }
        }
    }
    static Map<String,Object> map(Object... pairs) {
        Map<String,Object> result=new LinkedHashMap<>();
        for(int i=0;i<pairs.length;i+=2) result.put((String)pairs[i],pairs[i+1]);
        return result;
    }
    static double number(Object o,String name) throws Exception {return ((Number)field(o,name)).doubleValue();}
    static double[][] matrix(Object transform) throws Exception {
        double[][] m={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
        int type=((Number)field(transform,"_type")).intValue();
        if(type==0) return m;
        if(type>=3) {
            Object source=field(transform,"_matrix");
            // Clyde Matrix4f names elements column first, row second.
            for(int r=0;r<4;r++) for(int c=0;c<4;c++) m[r][c]=number(source,"m"+c+r);
            return m;
        }
        Object t=field(transform,"_translation"), q=field(transform,"_rotation");
        double x=number(q,"x"),y=number(q,"y"),z=number(q,"z"),w=number(q,"w");
        double n=Math.sqrt(x*x+y*y+z*z+w*w);
        if(n<1e-12) throw new IOException("Zero-length quaternion");
        x/=n;y/=n;z/=n;w/=n;
        double s=type==2?number(transform,"_scale"):1;
        m[0][0]=s*(1-2*y*y-2*z*z);m[0][1]=s*(2*x*y-2*z*w);m[0][2]=s*(2*x*z+2*y*w);
        m[1][0]=s*(2*x*y+2*z*w);m[1][1]=s*(1-2*x*x-2*z*z);m[1][2]=s*(2*y*z-2*x*w);
        m[2][0]=s*(2*x*z-2*y*w);m[2][1]=s*(2*y*z+2*x*w);m[2][2]=s*(1-2*x*x-2*y*y);
        m[0][3]=number(t,"x");m[1][3]=number(t,"y");m[2][3]=number(t,"z");
        return m;
    }
    static void bone(Object node,int parent) throws Exception {
        String name=(String)field(node,"name");
        if(defaults.containsKey(name)) throw new IOException("Duplicate bone: "+name);
        Object transform=field(node,"transform"); defaults.put(name,transform);
        int index=bones.size();bones.add(map("name",name,"parent",parent,"matrix",matrix(transform)));
        for(Object child:(Object[])field(node,"children")) bone(child,index);
    }
    static Map<String,Object> clip(Object impl,String name) throws Exception {
        String[] targets=(String[])field(impl,"targets");
        Object[] frames=(Object[])field(impl,"transforms");
        if(targets==null||frames==null||frames.length==0) throw new IOException("Empty imported animation");
        double rate=number(impl,"rate")*number(impl,"speed");
        if(!Double.isFinite(rate)||rate<=0) throw new IOException("Non-positive animation rate");
        List<String> missing=new ArrayList<>();
        for(String target:targets) if(!defaults.containsKey(target)) missing.add(target);
        // Delegate modifiers to the game's implementation with actual bind-pose defaults.
        Class<?> transformClass=Class.forName("com.threerings.math.Transform3D");
        Object rest=Array.newInstance(transformClass,targets.length);
        for(int i=0;i<targets.length;i++) Array.set(rest,i,defaults.containsKey(targets[i])?
            defaults.get(targets[i]):transformClass.getConstructor().newInstance());
        frames=(Object[])impl.getClass().getMethod("getModifiedTransforms",rest.getClass()).invoke(impl,rest);
        boolean reverse=(Boolean)field(impl,"reverseTransforms");
        List<Object> samples=new ArrayList<>();
        for(int f=0;f<frames.length;f++) {
            Object[] row=(Object[])frames[reverse?frames.length-1-f:f];
            if(row.length!=targets.length) throw new IOException("Target/frame width mismatch");
            List<Object> values=new ArrayList<>();
            for(Object transform:row) values.add(transform==null?null:matrix(transform));
            samples.add(values);
        }
        return map("name",name,"rate",rate,"loop",field(impl,"loop"),
            "skipLastFrame",field(impl,"skipLastFrame"),"targets",targets,"frames",samples,
            "transition",field(impl,"transition"),"sourceType",impl.getClass().getName(),"ignoredTargets",missing);
    }
    static String json(Object value) {
        if(value==null)return "null";
        if(value instanceof String) {
            StringBuilder b=new StringBuilder("\"");
            for(char c:((String)value).toCharArray()) {
                if(c=='"'||c=='\\') b.append('\\').append(c);
                else if(c<32)b.append(String.format("\\u%04x",(int)c));else b.append(c);
            }
            return b.append('"').toString();
        }
        if(value instanceof Number) {
            if(!Double.isFinite(((Number)value).doubleValue()))throw new IllegalArgumentException("Nonfinite value");
            return value.toString();
        }
        if(value instanceof Boolean)return value.toString();
        if(value instanceof Map) {
            List<String> parts=new ArrayList<>();
            ((Map<?,?>)value).forEach((k,v)->parts.add(json(k.toString())+":"+json(v)));
            return "{"+String.join(",",parts)+"}";
        }
        List<String> parts=new ArrayList<>();
        if(value instanceof Iterable) for(Object o:(Iterable<?>)value) parts.add(json(o));
        else if(value.getClass().isArray()) for(int i=0;i<Array.getLength(value);i++)parts.add(json(Array.get(value,i)));
        else throw new IllegalArgumentException("Cannot serialize "+value.getClass());
        return "["+String.join(",",parts)+"]";
    }
    public static void main(String[] args) throws Exception {
        if(args.length!=2)throw new IllegalArgumentException("Usage: KnightAnimationExport <rsrc directory> <new output directory>");
        Path rsrc=Paths.get(args[0]).toAbsolutePath(),out=Paths.get(args[1]).toAbsolutePath();
        if(Files.exists(out))throw new IOException("Use a new output directory: "+out);
        Object model=read(rsrc.resolve("character/pc/model.dat"));
        bone(field(field(model,"implementation"),"root"),-1);
        List<Object> clips=new ArrayList<>(),skipped=new ArrayList<>();
        List<Path> paths;
        try(Stream<Path> stream=Files.walk(rsrc.resolve("character/pc"))) {
            paths=stream.filter(p->p.toString().endsWith(".dat")&&Files.isRegularFile(p)).sorted().collect(Collectors.toList());
        }
        for(Path path:paths) {
            String name=rsrc.relativize(path).toString().replace('\\','/');
            try {
                Object config=read(path);
                if(!config.getClass().getName().equals("com.threerings.opengl.model.config.AnimationConfig"))continue;
                Object impl=field(config,"implementation");
                if(!impl.getClass().getName().endsWith("AnimationConfig$Imported")) {
                    skipped.add(map("file",name,"reason","Requires composition or procedural evaluation","type",impl.getClass().getName()));continue;
                }
                clips.add(clip(impl,name.substring("character/pc/".length(),name.length()-4)));
            } catch(Exception e) {
                Throwable cause=e;while(cause.getCause()!=null)cause=cause.getCause();
                skipped.add(map("file",name,"reason",cause.toString()));
            }
        }
        if(clips.isEmpty())throw new IOException("No clips exported; check game version and resource directory. "+json(skipped));
        Files.createDirectories(out);
        Files.writeString(out.resolve("knight-animations.json"),json(map("format","trs-knight-animations-1",
            "coordinateSystem","Clyde Z-up; source local matrices, game units","bones",bones,"clips",clips)));
        Files.writeString(out.resolve("export-report.json"),json(map("boneCount",bones.size(),"clipCount",clips.size(),"skipped",skipped)));
        System.out.println("Exported "+clips.size()+" clips and "+bones.size()+" bones. Skipped "+skipped.size()+" entries; see export-report.json.");
    }
}
