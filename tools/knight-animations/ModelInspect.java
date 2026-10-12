import java.lang.reflect.Array;
import java.nio.file.*;
import java.util.*;

/** Read-only summary for the Qt model browser. The game installation supplies the model classes. */
public final class ModelInspect {
    static Object field(Object value,String name)throws Exception{return KnightAnimationExport.field(value,name);}
    static Object opt(Object value,String name){try{return field(value,name);}catch(Exception ignored){return null;}}
    static Map<String,Object> map(Object...items){return KnightAnimationExport.map(items);}
    static String label(Object value){return value==null?"":String.valueOf(value);}
    static List<Object> children(Object value){
        List<Object> result=new ArrayList<>();
        if(value!=null&&value.getClass().isArray())for(int i=0;i<Array.getLength(value);i++)result.add(Array.get(value,i));
        return result;
    }
    static Map<String,Object> mesh(Object visible)throws Exception {
        Object geometry=opt(visible,"geometry");
        Object texture=opt(visible,"texture");
        Object vertices=geometry==null?null:geometry.getClass().getMethod("getVertexCount").invoke(geometry);
        return map("label",texture==null?"Mesh":label(texture),"kind","mesh","vertices",vertices,"children",Collections.emptyList());
    }
    static List<Object> meshes(Object set)throws Exception {
        List<Object> result=new ArrayList<>();
        for(Object visible:children(opt(set,"visible")))result.add(mesh(visible));
        return result;
    }
    static Map<String,Object> node(Object value,int depth)throws Exception {
        String name=label(opt(value,"name"));
        List<Object> result=new ArrayList<>();
        Object visible=opt(value,"visible");
        if(visible!=null){
            if(visible.getClass().isArray())for(Object item:children(visible))result.add(mesh(item));
            else result.add(mesh(visible));
        }
        if(opt(value,"geometry")!=null)result.add(mesh(value));
        if(depth<40)for(Object child:children(opt(value,"children")))result.add(node(child,depth+1));
        return map("label",name.isEmpty()?"Node":name,"kind","node","children",result);
    }
    static Map<String,Object> inspect(Path rsrc,Path path)throws Exception {
        if(!path.startsWith(rsrc))throw new IllegalArgumentException("Model must be inside the game resource directory");
        System.setProperty("resource_dir",rsrc.toString());
        Object config=KnightAnimationExport.read(path);
        if(!config.getClass().getName().equals("com.threerings.opengl.model.config.ModelConfig"))
            throw new IllegalArgumentException("Selected .dat is not a ModelConfig: "+config.getClass().getName());
        Object impl=field(config,"implementation");
        String type=impl==null?"Empty":impl.getClass().getSimpleName();
        List<Object> hierarchy=new ArrayList<>(),parameters=new ArrayList<>(),variants=new ArrayList<>();
        String selected="";
        if(type.equals("StaticConfig"))hierarchy.addAll(meshes(opt(impl,"meshes")));
        else if(type.equals("StaticSetConfig")){
            Object options=opt(impl,"meshes");
            selected=label(opt(impl,"model"));
            if(options instanceof Map)for(Map.Entry<?,?>entry:((Map<?,?>)options).entrySet()){
                String key=label(entry.getKey());variants.add(key);
                hierarchy.add(map("label",key,"kind","variant","children",meshes(entry.getValue())));
            }
        }else if(type.equals("CompoundConfig")||type.equals("MergedStaticConfig")){
            for(Object component:children(opt(impl,"models")))hierarchy.add(map("label",label(opt(component,"model")),"kind","reference","children",Collections.emptyList()));
        }else if(opt(impl,"root")!=null){
            hierarchy.add(node(field(impl,"root"),0));
            for(Object item:children(opt(impl,"attachments")))hierarchy.add(map("label","Attachment: "+label(opt(item,"model")),"kind","reference","children",Collections.emptyList()));
        }
        for(Object parameter:children(opt(config,"parameters"))){
            List<Object> options=new ArrayList<>();
            for(Object item:children(opt(parameter,"options")))options.add(label(opt(item,"name")));
            parameters.add(map("name",label(opt(parameter,"name")),"kind",parameter.getClass().getSimpleName(),"options",options));
        }
        return map("source",path.toString(),"relativePath",rsrc.relativize(path).toString().replace('\\','/'),
            "bytes",Files.size(path),"implementation",type,"selectedVariant",selected,
            "variants",variants,"parameters",parameters,"hierarchy",hierarchy);
    }
    public static void main(String[]args)throws Exception {
        if(args.length!=2)throw new IllegalArgumentException("Usage: ModelInspect <rsrc directory> <model.dat>");
        Path rsrc=Paths.get(args[0]).toAbsolutePath().normalize(),path=Paths.get(args[1]).toAbsolutePath().normalize();
        System.out.println("TRS_MODEL_INSPECT:"+KnightAnimationExport.json(inspect(rsrc,path)));
    }
}
