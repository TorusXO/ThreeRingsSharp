import java.io.*;
import java.lang.reflect.*;
import java.nio.*;
import java.nio.file.*;
import java.util.*;

/** Static scene geometry export using the installed game's configuration resolver. */
public final class SceneExport {
    static Class<?> cm, refClass, modelClass, transformClass;
    static Object manager;
    static Object rootManager;
    static boolean actorsLoaded=false;
    static Path resources;
    static Path output;
    static Object resourceManager, colorPository;
    static Path legacyGlobals;
    static final Map<String,String> recolored=new LinkedHashMap<>();
    static final List<Object> meshes=new ArrayList<>(), instances=new ArrayList<>(), issues=new ArrayList<>(), metadata=new ArrayList<>();
    static final Map<String,List<Map<String,Object>>> assets=new LinkedHashMap<>();
    static final Map<String,Integer> meshIds=new LinkedHashMap<>();
    static final Map<String,Object> materials=new LinkedHashMap<>();
    static final Map<String,Object> placements=new LinkedHashMap<>();
    static final Map<String,Integer> kinds=new TreeMap<>(), modelKinds=new TreeMap<>();
    static final Set<String> issueKeys=new HashSet<>();
    static int errors=0;
    static Object f(Object o,String n)throws Exception{return KnightAnimationExport.field(o,n);}
    static Object opt(Object o,String n){try{return f(o,n);}catch(Exception e){return null;}}
    static Map<String,Object> map(Object...p){return KnightAnimationExport.map(p);}
    static Object call(Object o,String name)throws Exception{return o.getClass().getMethod(name).invoke(o);}
    static Object resolve(Object mgr,Class<?> type,Object ref)throws Exception {
        Object result=cm.getMethod("a",Class.class,refClass).invoke(mgr,type,ref);
        if(result==null)throw new IOException("Unresolved "+type.getSimpleName()+": "+ref);
        return result;
    }
    static String name(Object ref)throws Exception{return (String)call(ref,"getName");}
    static double[][] identity(){return new double[][]{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};}
    static double[][] matrix(Object o)throws Exception{return o==null?identity():KnightAnimationExport.matrix(o);}
    static double[][] mul(double[][]a,double[][]b){double[][]r=new double[4][4];for(int i=0;i<4;i++)for(int j=0;j<4;j++)for(int k=0;k<4;k++)r[i][j]+=a[i][k]*b[k][j];return r;}
    static Map<String,Object> transformed(Map<String,Object>part,double[][]t){Map<String,Object>r=new LinkedHashMap<>(part);r.put("matrix",Boolean.TRUE.equals(part.get("skybox"))?part.get("matrix"):mul(t,(double[][])part.get("matrix")));return r;}
    static Object snapshot(Object value,int depth){
        if(value==null||value instanceof String||value instanceof Number||value instanceof Boolean)return value;
        if(value instanceof Enum)return value.toString();
        if(depth>3)return map("class",value.getClass().getName());
        if(value.getClass().isArray()){List<Object>r=new ArrayList<>();for(int i=0;i<Math.min(Array.getLength(value),128);i++)r.add(snapshot(Array.get(value,i),depth+1));return r;}
        Map<String,Object>r=map("class",value.getClass().getName());
        for(Field field:value.getClass().getFields())if(!Modifier.isStatic(field.getModifiers())&&!Modifier.isTransient(field.getModifiers()))try{r.put(field.getName(),snapshot(field.get(value),depth+1));}catch(Exception ignored){}
        return r;
    }
    static void marker(List<Map<String,Object>>parts,String source,String type,Object impl){parts.add(map("mesh",null,"matrix",identity(),"kind",type,"detail",source,"configuration",snapshot(impl,0)));}
    static void issue(String source,String reason){if(issueKeys.add(source+reason)){
        boolean limitation=reason.startsWith("Runtime feature retained")||reason.startsWith("GUI text billboard retained");
        if(!limitation)errors++;
        issues.add(map("source",source,"reason",reason,"severity",limitation?"limitation":"error"));
    }}
    static Throwable cause(Throwable e){while(e.getCause()!=null)e=e.getCause();return e;}
    static Object createManager(Path rsrc)throws Exception {
        resources=rsrc;
        System.setProperty("resource_dir",rsrc.toString());
        cm=Class.forName("com.threerings.config.ConfigManager");refClass=Class.forName("com.threerings.config.ConfigReference");
        modelClass=Class.forName("com.threerings.opengl.model.config.ModelConfig");transformClass=Class.forName("com.threerings.math.Transform3D");
        Class<?> rm=Class.forName("com.threerings.b.f"),msg=Class.forName("com.threerings.util.R");
        resourceManager=rm.getConstructor(String.class).newInstance("rsrc");
        Object root=cm.getConstructor(rm,msg,String.class).newInstance(resourceManager,msg.getConstructor(String.class).newInstance("rsrc.i18n"),"config/");
        // Loading unrelated gameplay groups fails on stripped game classes. Initialize
        // only the render/scene groups; retain the game's resource-class registry.
        Method props=cm.getDeclaredMethod("kV");props.setAccessible(true);props.invoke(root);
        Field cache=cm.getDeclaredField("_resources");cache.setAccessible(true);cache.set(root,new HashMap<>());
        Method load=cm.getDeclaredMethod("d",Class.class,java.util.function.Consumer.class);load.setAccessible(true);
        for(String type:new String[]{"com.threerings.tudey.config.TileConfig","com.threerings.tudey.config.PlaceableConfig","com.threerings.opengl.material.config.MaterialConfig","com.threerings.opengl.renderer.config.TextureConfig"})
            load.invoke(root,Class.forName(type),(java.util.function.Consumer<Exception>)(e)->issue(type,e.toString()));
        Path legacyPlaceables=rsrc.getParent().resolve("placeable_projectx.dat");
        if(!Files.isRegularFile(rsrc.resolve("config/placeable.dat"))&&Files.isRegularFile(legacyPlaceables)){
            Object group=cm.getMethod("bd",Class.class).invoke(root,Class.forName("com.threerings.tudey.config.PlaceableConfig"));
            Object[]configs=(Object[])KnightAnimationExport.read(legacyPlaceables);
            Method add=group.getClass().getMethod("a",Class.forName("com.threerings.config.ManagedConfig"));
            for(Object config:configs)if(config!=null)add.invoke(group,config);
            System.out.println("Loaded external placeable config: "+legacyPlaceables+" ("+configs.length+" definitions)");
        }
        // Global groups contain stripped gameplay types; retain the readable entries.
        Class<?> global=Class.forName("com.threerings.tudey.config.SceneGlobalConfig");
        Class<?> groupClass=Class.forName("com.threerings.config.ConfigGroup");
        Object globals=groupClass.getConstructor(Class.class).newInstance(global);
        Field managerField=groupClass.getDeclaredField("_cfgmgr");managerField.setAccessible(true);managerField.set(globals,root);
        ((Map)f(root,"_groups")).put(global,globals);
        Path globalFile=rsrc.resolve("config/scene_global.dat");
        Object[] globalConfigs=legacyGlobals==null?(Files.isRegularFile(globalFile)?(Object[])KnightAnimationExport.read(globalFile):new Object[0]):readLegacyGlobals(legacyGlobals);
        for(Object config:globalConfigs)
            if(config!=null)groupClass.getMethod("a",Class.forName("com.threerings.config.ManagedConfig")).invoke(globals,config);
        rootManager=root;return root;
    }
    static Object[] readLegacyGlobals(Path path)throws Exception {
        // The old wrapper became abstract in current game jars. Migrate the XML
        // representation in memory, without changing either game or legacy backend.
        javax.xml.parsers.DocumentBuilderFactory factory=javax.xml.parsers.DocumentBuilderFactory.newInstance();
        factory.setFeature("http://apache.org/xml/features/disallow-doctype-decl",true);
        org.w3c.dom.Document document=factory.newDocumentBuilder().parse(path.toFile());
        org.w3c.dom.Element object=(org.w3c.dom.Element)document.getElementsByTagName("object").item(0);
        List<org.w3c.dom.Element>entries=new ArrayList<>();
        for(org.w3c.dom.Node n=object.getFirstChild();n!=null;n=n.getNextSibling())if(n instanceof org.w3c.dom.Element)entries.add((org.w3c.dom.Element)n);
        for(org.w3c.dom.Element entry:entries){
            org.w3c.dom.Element implementation=null;
            for(org.w3c.dom.Node n=entry.getFirstChild();n!=null;n=n.getNextSibling())if(n instanceof org.w3c.dom.Element&&n.getNodeName().equals("implementation"))implementation=(org.w3c.dom.Element)n;
            if(implementation==null||!(implementation.getAttribute("class").equals("com.threerings.tudey.config.SceneGlobalConfig$EnvironmentModel")||implementation.getAttribute("class").equals("com.threerings.tudey.config.SceneGlobalConfig$Camera"))){object.removeChild(entry);continue;}
            entry.setAttribute("class",implementation.getAttribute("class"));
            while(implementation.hasChildNodes())entry.appendChild(implementation.getFirstChild());
            entry.removeChild(implementation);
            org.w3c.dom.NodeList paths=entry.getElementsByTagName("paths");
            for(int i=0;i<paths.getLength();i++)paths.item(i).setTextContent(paths.item(i).getTextContent().replace("implementation.",""));
        }
        ByteArrayOutputStream bytes=new ByteArrayOutputStream();
        javax.xml.transform.TransformerFactory.newInstance().newTransformer().transform(new javax.xml.transform.dom.DOMSource(document),new javax.xml.transform.stream.StreamResult(bytes));
        Object reader=Class.forName("com.threerings.export.XMLImporter").getConstructor(InputStream.class).newInstance(new ByteArrayInputStream(bytes.toByteArray()));
        try{return (Object[])call(reader,"qe");}finally{call(reader,"close");}
    }
    static float[][] array(Object config,int count)throws Exception {
        if(config==null)return null;
        FloatBuffer buf=(FloatBuffer)f(config,"floatArray");
        if(buf==null)throw new IOException("No floating-point vertex buffer");
        int size=((Number)f(config,"size")).intValue(),stride=((Number)f(config,"stride")).intValue()/4,offset=((Number)f(config,"offset")).intValue()/4;
        if(stride==0)stride=size;
        float[][]result=new float[count][size];
        for(int i=0;i<count;i++)for(int j=0;j<size;j++)result[i][j]=buf.get(offset+i*stride+j);
        return result;
    }
    static void textureFiles(Object value,Set<String> files,Set<Object> visited,int depth,Object mgr)throws Exception {
        if(value==null||depth>16||!visited.add(value))return;
        if(value.getClass().getName().endsWith("TextureConfig$Original2D$ImageFile")){
            String file=(String)f(value,"file");Object configs=f(value,"colorizations");
            if(configs!=null&&Array.getLength(configs)>0){
                String key=file+KnightAnimationExport.json(snapshot(configs,0));
                String result=recolored.get(key);
                if(result==null){
                    Class<?> cp=Class.forName("com.threerings.media.image.ColorPository"), color=Class.forName("com.threerings.media.image.b");
                    if(colorPository==null)colorPository=cp.getMethod("a",resourceManager.getClass()).invoke(null,resourceManager);
                    Object colors=Array.newInstance(color,Array.getLength(configs));
                    for(int i=0;i<Array.getLength(configs);i++)Array.set(colors,i,Array.get(configs,i).getClass().getMethod("getColorization",cp).invoke(Array.get(configs,i),colorPository));
                    java.awt.image.BufferedImage image=javax.imageio.ImageIO.read(resources.resolve(file).toFile());
                    if(image==null)throw new IOException("Cannot decode recolor source "+file);
                    image=(java.awt.image.BufferedImage)Class.forName("com.threerings.media.image.d").getMethod("a",java.awt.image.BufferedImage.class,colors.getClass()).invoke(null,image,colors);
                    String relative="textures/recolored_"+recolored.size()+".png";
                    Path target=output.resolve(relative);Files.createDirectories(target.getParent());
                    if(!javax.imageio.ImageIO.write(image,"png",target.toFile()))throw new IOException("PNG encoder unavailable");
                    result="@bundle/"+relative;
                    recolored.put(key,result);
                }
                files.add(result);return;
            }
        }
        if(value instanceof String){String s=(String)value;if(s.matches("(?i).+\\.(png|jpg|jpeg|tga|dds)"))files.add(s);return;}
        if(value.getClass().isArray()){for(int i=0;i<Array.getLength(value);i++)textureFiles(Array.get(value,i),files,visited,depth+1,mgr);return;}
        if(!value.getClass().getName().startsWith("com.threerings"))return;
        for(Field field:value.getClass().getFields()){
            if(Modifier.isStatic(field.getModifiers()))continue;
            Object child=field.get(value);
            if(child!=null&&refClass.isInstance(child)&&field.getGenericType() instanceof ParameterizedType){
                Type arg=((ParameterizedType)field.getGenericType()).getActualTypeArguments()[0];
                if(arg instanceof Class && ((Class<?>)arg).getName().contains("TextureConfig")){
                    try{child=resolve(mgr,(Class<?>)arg,child);}catch(Exception e){issue(name(child),cause(e).toString());continue;}
                }
            }
            textureFiles(child,files,visited,depth+1,mgr);
        }
    }
    static String material(Object impl,Object mesh,Object mgr,String source)throws Exception {
        String texture=String.valueOf(f(mesh,"texture")),tag=String.valueOf(f(mesh,"tag"));
        Object ref=null;
        Object mappings=opt(impl,"materialMappings");
        if(mappings!=null)for(Object mapping:(Object[])mappings)
            if(Objects.equals(f(mapping,"texture"),texture)&&Objects.equals(f(mapping,"tag"),tag)){ref=f(mapping,"material");break;}
        String key=ref==null?source+"|"+texture:ref.toString();
        if(materials.containsKey(key))return key;
        Set<String> files=new LinkedHashSet<>();
        if(ref!=null)try{textureFiles(resolve(mgr,Class.forName("com.threerings.opengl.material.config.MaterialConfig"),ref),files,Collections.newSetFromMap(new IdentityHashMap<>()),0,mgr);}
        catch(Exception e){issue(source+" material "+key,cause(e).toString());}
        if(files.isEmpty()){
            Path relative=Paths.get(source).getParent();
            if(relative!=null&&Files.isRegularFile(resources.resolve(relative).resolve(texture)))files.add(relative.resolve(texture).toString().replace('\\','/'));
            else if(Files.isRegularFile(resources.resolve(texture)))files.add(texture);
        }
        Map<String,Object>parameters=new LinkedHashMap<>();
        if(ref!=null)for(Map.Entry<?,?>arg:((Map<?,?>)f(ref,"_arguments")).entrySet()){
            Object v=arg.getValue();if(v instanceof String||v instanceof Number||v instanceof Boolean)parameters.put(arg.getKey().toString(),v);
            else if(v!=null&&v.getClass().getName().equals("com.threerings.opengl.renderer.Color4f"))parameters.put(arg.getKey().toString(),new Object[]{f(v,"r"),f(v,"g"),f(v,"b"),f(v,"a")});
        }
        materials.put(key,map("name",texture,"reference",ref==null?null:ref.toString(),"textures",files,"parameters",parameters));
        return key;
    }
    static int mesh(Object visible,Object impl,Object mgr,String source)throws Exception {
        Object geometry=f(visible,"geometry");
        String mat=material(impl,visible,mgr,source);
        String key=System.identityHashCode(geometry)+"|"+mat;
        if(meshIds.containsKey(key))return meshIds.get(key);
        int count=((Number)call(geometry,"getVertexCount")).intValue();
        float[][]positions=array(f(geometry,"vertexArray"),count),normals=array(opt(geometry,"normalArray"),count),uv=null;
        Object[] tex=(Object[])opt(geometry,"texCoordArrays");
        Map<String,Object> matData=(Map<String,Object>)materials.get(mat);
        Object coord=((Map<String,Object>)matData.get("parameters")).get("Coord Set");int uvIndex=coord instanceof Number?((Number)coord).intValue():0;
        if(tex!=null&&tex.length>0)uv=array(tex[Math.min(Math.max(uvIndex,0),tex.length-1)],count);
        Object raw=opt(geometry,"indices");int[]indices;
        if(raw instanceof ShortBuffer){ShortBuffer b=(ShortBuffer)raw;indices=new int[b.limit()];for(int i=0;i<indices.length;i++)indices[i]=Short.toUnsignedInt(b.get(i));}
        else {int first=opt(geometry,"first")==null?0:((Number)f(geometry,"first")).intValue();int n=opt(geometry,"count")==null?count:((Number)f(geometry,"count")).intValue();indices=new int[n];for(int i=0;i<n;i++)indices[i]=first+i;}
        List<int[]>faces=new ArrayList<>();String mode=f(geometry,"mode").toString();
        if(mode.equals("TRIANGLES")){for(int i=0;i+2<indices.length;i+=3)faces.add(new int[]{indices[i],indices[i+1],indices[i+2]});}
        else if(mode.equals("TRIANGLE_STRIP")){for(int i=0;i+2<indices.length;i++){int a=indices[i],b=indices[i+1],c=indices[i+2];if(a!=b&&a!=c&&b!=c)faces.add(i%2==0?new int[]{a,b,c}:new int[]{b,a,c});}}
        else if(mode.equals("TRIANGLE_FAN")){for(int i=1;i+1<indices.length;i++)faces.add(new int[]{indices[0],indices[i],indices[i+1]});}
        else if(mode.equals("QUADS")){for(int i=0;i+3<indices.length;i+=4)faces.add(new int[]{indices[i],indices[i+1],indices[i+2],indices[i+3]});}
        else throw new IOException("Unsupported geometry mode "+mode);
        for(int[]face:faces)for(int v:face)if(v<0||v>=count)throw new IOException("Vertex index out of bounds");
        int id=meshes.size();meshIds.put(key,id);
        meshes.add(map("name",source+"/"+f(visible,"texture"),"positions",positions,"normals",normals,"uv",uv,"colors",array(opt(geometry,"colorArray"),count),"faces",faces,"material",mat));
        return id;
    }
    static void meshSet(Object set,Object impl,Object mgr,String source,double[][]transform,List<Map<String,Object>>parts)throws Exception {
        if(set==null)return;
        for(Object visible:(Object[])f(set,"visible"))try{parts.add(map("mesh",mesh(visible,impl,mgr,source),"matrix",transform));}
        catch(Exception e){issue(source,cause(e).toString());}
    }
    static void node(Object node,Object impl,Object mgr,String source,double[][]parent,List<Map<String,Object>>parts,Map<String,double[][]>nodes)throws Exception {
        double[][]world=mul(parent,matrix(f(node,"transform")));
        nodes.put((String)f(node,"name"),world);
        Object visible=opt(node,"visible");if(visible!=null){if(visible.getClass().isArray())for(Object v:(Object[])visible)parts.add(map("mesh",mesh(v,impl,mgr,source),"matrix",world));else parts.add(map("mesh",mesh(visible,impl,mgr,source),"matrix",world));}
        Object geometry=opt(node,"geometry");if(geometry!=null)parts.add(map("mesh",mesh(node,impl,mgr,source),"matrix",world));
        for(Object child:(Object[])f(node,"children"))node(child,impl,mgr,source,world,parts,nodes);
    }
    static List<Map<String,Object>> asset(Object reference,Object mgr,Set<String>stack)throws Exception {
        String source=name(reference),key=System.identityHashCode(mgr)+"|"+reference;
        if(assets.containsKey(key))return assets.get(key);
        if(stack.size()>40||!stack.add(key))throw new IOException("Cyclic model reference "+source);
        List<Map<String,Object>>parts=new ArrayList<>();
        try{
            Object config=resolve(mgr,modelClass,reference),impl=call(config,"getOriginal");
            if(impl==null)throw new IOException("Missing original model implementation");
            Object own=call(config,"getConfigManager");if(own!=null)mgr=own;
            String type=impl.getClass().getSimpleName();modelKinds.merge(type,1,Integer::sum);
            if(type.equals("StaticConfig"))meshSet(f(impl,"meshes"),impl,mgr,source,identity(),parts);
            else if(type.equals("StaticSetConfig")){
                Object set=((Map<?,?>)f(impl,"meshes")).get(f(impl,"model"));
                if(set==null)throw new IOException("Static-set selection not found: "+f(impl,"model"));
                meshSet(set,impl,mgr,source,identity(),parts);
            }else if(type.equals("CompoundConfig")||type.equals("MergedStaticConfig")){
                for(Object component:(Object[])f(impl,"models")){
                    Object child=f(component,"model");if(child==null)continue;
                    double[][]t=matrix(f(component,"transform"));
                    try{for(Map<String,Object>part:asset(child,mgr,stack))parts.add(transformed(part,t));}
                    catch(Exception e){issue(source+" -> "+child,cause(e).toString());}
                }
            }else if(type.equals("Schemed")){
                Method select=impl.getClass().getDeclaredMethod("getModel",Class.forName("com.threerings.expr.aq"));select.setAccessible(true);
                Object child=select.invoke(impl,new Object[]{null});if(child!=null)parts.addAll(asset(child,mgr,stack));
            }else if(type.equals("ConditionalConfig")){
                Object child=f(impl,"defaultModel"),t=f(impl,"defaultTransform");
                for(Object c:(Object[])f(impl,"cases")){
                    Object expression=f(c,"condition");
                    Object evaluator=expression.getClass().getMethod("a",Class.forName("com.threerings.expr.aq")).invoke(expression,new Object[]{null});
                    Method evaluate=Class.forName("com.threerings.expr.BooleanExpression$a").getMethod("qA");
                    if((Boolean)evaluate.invoke(evaluator)){child=f(c,"model");t=f(c,"transform");break;}
                }
                if(child!=null)for(Map<String,Object>part:asset(child,mgr,stack))parts.add(transformed(part,matrix(t)));
            }else if(type.equals("Wrapper")&&opt(impl,"actor")!=null){
                if(!actorsLoaded){
                    actorsLoaded=true;Class<?>actorClass=Class.forName("com.threerings.tudey.config.ActorConfig");
                    Object group=Class.forName("com.threerings.config.ConfigGroup").getConstructor(Class.class).newInstance(actorClass);
                    Field cfgmgr=group.getClass().getDeclaredField("_cfgmgr");cfgmgr.setAccessible(true);cfgmgr.set(group,rootManager);
                    ((Map)f(rootManager,"_groups")).put(actorClass,group);
                    Method add=group.getClass().getMethod("a",Class.forName("com.threerings.config.ManagedConfig"));
                    for(Object actorConfig:(Object[])KnightAnimationExport.read(resources.resolve("config/actor.dat")))if(actorConfig!=null)add.invoke(group,actorConfig);
                }
                Method get=impl.getClass().getDeclaredMethod("getModelConfig",cm);get.setAccessible(true);
                Object wrapped=get.invoke(impl,mgr);
                if(wrapped!=null)parts.addAll(asset(call(wrapped,"getReference"),mgr,stack));
                else issue(source,"Actor wrapper resolved to no visual model");
            }else if(type.equals("ViewerAffecterConfig")&&opt(impl,"effect")!=null&&f(impl,"effect").getClass().getSimpleName().equals("Skybox")){
                Object effect=f(impl,"effect"),child=f(effect,"model");
                if(child!=null){
                    double[][] origin=identity();Object v=f(effect,"translationOrigin");
                    origin[0][3]=((Number)f(v,"x")).doubleValue();origin[1][3]=((Number)f(v,"y")).doubleValue();origin[2][3]=((Number)f(v,"z")).doubleValue();
                    for(Map<String,Object>part:asset(child,mgr,stack)){
                        Map<String,Object>sky=transformed(part,origin);sky.put("skybox",true);
                        sky.put("configuration",snapshot(effect,0));parts.add(sky);
                    }
                }
            }else if(type.equals("ComponentBillboardConfig")){
                issue(source,"GUI text billboard retained as metadata; not a stored mesh");
                marker(parts,source,type,impl);
            }else if(opt(impl,"root")!=null){
                Map<String,double[][]>nodes=new HashMap<>();
                meshSet(opt(impl,"skin"),impl,mgr,source,identity(),parts);node(f(impl,"root"),impl,mgr,source,identity(),parts,nodes);
                if(opt(impl,"attachments")!=null)for(Object attachment:(Object[])f(impl,"attachments")){
                    Object child=f(attachment,"model");if(child==null)continue;
                    String target=(String)f(attachment,"node");double[][]base=nodes.get(target);
                    if(base==null){issue(source,"Attachment node not found: "+target);continue;}
                    double[][]t=mul(base,matrix(f(attachment,"transform")));
                    try{for(Map<String,Object>part:asset(child,mgr,stack))parts.add(transformed(part,t));}
                    catch(Exception e){issue(source+" attachment "+child,cause(e).toString());}
                }
            }else if(source.startsWith("editor/")){ /* editor-only markers remain as metadata */ }
            else {issue(source,"Runtime feature retained as metadata: "+impl.getClass().getName());marker(parts,source,type,impl);}
            assets.put(key,parts);return parts;
        }finally{stack.remove(key);}
    }
    public static void main(String[]args)throws Exception {
        if(args.length<3||args.length>4)throw new IllegalArgumentException("Usage: SceneExport <rsrc> <scene-file> <new-output-dir> [legacy-scene-global.xml]");
        if(args.length==4)legacyGlobals=Paths.get(args[3]);
        Path rsrc=Paths.get(args[0]).toAbsolutePath(),scenePath=Paths.get(args[1]).toAbsolutePath(),out=Paths.get(args[2]).toAbsolutePath();
        if(Files.exists(out))throw new IOException("Use a new output directory: "+out);
        output=out;
        Object root=createManager(rsrc);
        // A model config can be exported independently, including a skybox wrapper.
        Object scene=KnightAnimationExport.read(scenePath);
        if(modelClass.isInstance(scene)){
            if(!scenePath.startsWith(rsrc))throw new IOException("Model must be inside the resource directory");
            String source=rsrc.relativize(scenePath).toString().replace('\\','/');
            Object reference=refClass.getConstructor(String.class).newInstance(source);
            for(Map<String,Object>part:asset(reference,root,new HashSet<>()))instances.add(map("entry",0,"type","Model","model",source,"reference",source,"mesh",part.get("mesh"),"matrix",part.get("matrix"),"skybox",part.get("skybox"),"kind",part.get("kind"),"detail",part.get("detail"),"configuration",part.get("configuration")));
            Files.createDirectories(out);
            Files.writeString(out.resolve("scene.json"),KnightAnimationExport.json(map("format","trs-scene-1","name",source,"source",scenePath.toString(),"resourceDirectory",rsrc.toString(),"meshes",meshes,"materials",materials,"instances",instances,"metadata",metadata)));
            Files.writeString(out.resolve("scene-report.json"),KnightAnimationExport.json(map("meshCount",meshes.size(),"instanceCount",instances.size(),"issues",issues)));
            if(meshes.isEmpty())throw new IOException("No mesh geometry resolved; see scene-report.json for runtime-only effects");
            if(errors>0)throw new IOException("Export contains "+errors+" unresolved errors; see scene-report.json");
            System.out.println("Model export: "+meshes.size()+" meshes, "+instances.size()+" instances.");return;
        }
        scene.getClass().getMethod("init",cm).invoke(scene,root);manager=call(scene,"getConfigManager");
        Collection<?>entries=(Collection<?>)call(scene,"adH");int entryId=0,visualEntries=0,emptyEntries=0;
        for(Object entry:entries){
            String kind=entry.getClass().getSimpleName();kinds.merge(kind,1,Integer::sum);int id=entryId++;
            try{
                Object reference=call(entry,"getReference");
                if(kind.equals("GlobalEntry")){
                    Object original=entry.getClass().getMethod("az",cm).invoke(entry,manager);
                    if(original==Class.forName("com.threerings.tudey.config.SceneGlobalConfig").getField("bSl").get(null))issue("Global "+reference,"Unresolved global configuration");
                    metadata.add(map("id",id,"type",kind,"reference",String.valueOf(reference),"configuration",snapshot(original,0)));
                    Object model=opt(original,"model");
                    if(model!=null)for(Map<String,Object>part:asset(model,manager,new HashSet<>())){
                        Map<String,Object>placed=transformed(part,matrix(opt(original,"transform")));
                        instances.add(map("entry",id,"type",kind,"model",name(model),"reference",String.valueOf(reference),"mesh",part.get("mesh"),"matrix",placed.get("matrix"),"skybox",part.get("skybox"),"configuration",part.get("configuration")));
                    }
                    continue;
                }
                if(!kind.equals("TileEntry")&&!kind.equals("PlaceableEntry")){metadata.add(map("id",id,"type",kind,"reference",String.valueOf(reference)));continue;}
                String method=kind.equals("TileEntry")?"aC":"aB";
                Object original=entry.getClass().getMethod(method,cm).invoke(entry,manager);
                if(original==null)throw new IOException("Missing scene entry configuration: "+reference);
                Object model=f(original,"model");if(model==null){emptyEntries++;continue;}
                if(name(model).equals("editor/error/model.dat"))throw new IOException("Game returned an error placeholder for "+reference+"; required placeable/tile config is missing");
                Object transform;
                if(kind.equals("TileEntry")){
                    Object location=f(entry,"_location");
                    placements.put(Integer.toString(id),map("type",kind,"x",f(location,"x"),"y",f(location,"y"),"width",f(original,"width"),"height",f(original,"height"),"rotation",f(entry,"rotation"),"elevation",f(entry,"elevation"),"elevationScale",Class.forName("com.threerings.tudey.util.p").getMethod("hc",int.class).invoke(null,1)));
                    transform=transformClass.getConstructor().newInstance();
                    entry.getClass().getMethod("a",Class.forName("com.threerings.tudey.config.TileConfig$Original"),transformClass).invoke(entry,original,transform);
                }else {transform=f(entry,"transform");placements.put(Integer.toString(id),map("type",kind,"matrix",matrix(transform)));}
                List<Map<String,Object>>parts=asset(model,manager,new HashSet<>());
                String modelName=name(model);
                if(parts.isEmpty()){emptyEntries++;metadata.add(map("id",id,"type",kind,"reference",String.valueOf(reference),"model",modelName,"matrix",matrix(transform)));continue;}
                visualEntries++;
                for(Map<String,Object>part:parts)instances.add(map("entry",id,"type",kind,"model",modelName,"reference",String.valueOf(reference),"mesh",part.get("mesh"),"matrix",transformed(part,matrix(transform)).get("matrix"),"localMatrix",part.get("matrix"),"skybox",part.get("skybox"),"editorOnly",modelName.startsWith("editor/"),"kind",part.get("kind"),"detail",part.get("detail"),"configuration",part.get("configuration")));
            }catch(Exception e){issue("Entry "+id+" "+kind+" "+entry,cause(e).toString());}
            if(id%2000==0)System.out.println("Read "+id+" / "+entries.size()+" entries");
        }
        Files.createDirectories(out);
        Files.writeString(out.resolve("scene.json"),KnightAnimationExport.json(map("format","trs-scene-1","name",f(scene,"name"),"source",scenePath.toString(),"resourceDirectory",rsrc.toString(),"meshes",meshes,"materials",materials,"instances",instances,"placements",placements,"metadata",metadata)));
        Files.writeString(out.resolve("scene-report.json"),KnightAnimationExport.json(map("entryCount",entries.size(),"entryKinds",kinds,"visualEntries",visualEntries,"emptyEntries",emptyEntries,"meshCount",meshes.size(),"instanceCount",instances.size(),"modelKinds",modelKinds,"issues",issues)));
        System.out.println("Scene export: "+meshes.size()+" mesh definitions, "+instances.size()+" instances, "+issues.size()+" issues.");
        if(errors>0)throw new IOException("Export contains "+errors+" unresolved errors; see scene-report.json");
    }
}
