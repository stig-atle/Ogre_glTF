#include "Ogre_glTF_modelConverter.hpp"
#include "Ogre_glTF_doubleConverter.hpp"
#include "Ogre_glTF_common.hpp"
#include <OgreMesh2.h>
#include <OgreMeshManager2.h>
#include <OgreSubMesh2.h>

size_t Ogre_glTF_vertexBufferPart::getPartStride() const
{
	return buffer->elementSize() * perVertex;
}

Ogre_glTF_modelConverter::Ogre_glTF_modelConverter(tinygltf::Model& input) :
 model{ input }
{
}

Ogre::VertexBufferPackedVec Ogre_glTF_modelConverter::constructVertexBuffer(const std::vector<Ogre_glTF_vertexBufferPart>& parts) const
{
	Ogre::VertexElement2Vec vertexElements;

	size_t stride{ 0 }, strideInElements{ 0 };
	size_t vertexCount{ 0 }, previousVertexCount{ 0 };
	
	for(const auto& part : parts)
	{
		vertexElements.emplace_back(part.type, part.semantic);
		strideInElements += part.perVertex;
		stride += part.buffer->elementSize() * part.perVertex;
		vertexCount = part.vertexCount;

		//Sanity check
		if(previousVertexCount != 0)
		{
			if(vertexCount != previousVertexCount)
				throw std::runtime_error("Part of vertex buffer for the same primitive have different vertex counts!");
		}
		else previousVertexCount = vertexCount;
	}

	OgreLog("There will be " + std::to_string(vertexCount) + " vertices with a stride of " + std::to_string(stride) + " bytes");

	Ogre_glTF_geometryBuffer<float> finalBuffer(vertexCount * strideInElements);
	size_t bytesWrittenInCurrentStride{ 0 };
	for(size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
	{
		bytesWrittenInCurrentStride = 0;
		for(const auto& part : parts)
		{
			memcpy(finalBuffer.dataAddress() + (bytesWrittenInCurrentStride + vertexIndex * stride),
				   (part.buffer->dataAddress() + (vertexIndex * part.getPartStride())),
				   part.getPartStride());
			bytesWrittenInCurrentStride += part.getPartStride();
		}
	}

	Ogre::VertexBufferPackedVec vec;
	auto vertexBuffer = getVaoManager()->createVertexBuffer(vertexElements,
															vertexCount,
															Ogre::BT_IMMUTABLE,
															finalBuffer.data(),
															false);

	vec.push_back(vertexBuffer);
	return vec;
}

//TODO make this method thake the mesh id. Enumerate the meshes in the file before blindlessly loading the first one
Ogre::MeshPtr Ogre_glTF_modelConverter::getOgreMesh()
{
	OgreLog("Default scene" + std::to_string(model.defaultScene));
	const auto mainMeshIndex = (model.defaultScene != 0 ? model.nodes[model.scenes[model.defaultScene].nodes.front()].mesh : 0);
	const auto& mesh		 = model.meshes[mainMeshIndex];
	Ogre::Aabb boundingBox;
	OgreLog("Found mesh " + mesh.name + " in glTF file");

	auto OgreMesh = Ogre::MeshManager::getSingleton().getByName(mesh.name);
	if(OgreMesh)
	{
		OgreLog("Found mesh " + mesh.name + " in Ogre::MeshManager(v2)");
		return OgreMesh;
	}

	OgreLog("Loading mesh from glTF file");
	OgreLog("mesh has " + std::to_string(mesh.primitives.size()) + " primitives");
	OgreMesh = Ogre::MeshManager::getSingleton().createManual(mesh.name, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
	OgreLog("Created mesh on v2 MeshManager");

	for(const auto& primitive : mesh.primitives)
	{
		auto subMesh = OgreMesh->createSubMesh();
		OgreLog("Created one submesh");
		auto indexBuffer = extractIndexBuffer(primitive.indices);

		std::vector<Ogre_glTF_vertexBufferPart> parts;
		OgreLog("\tprimitive has : " + std::to_string(primitive.attributes.size()) + " atributes");
		for(const auto& atribute : primitive.attributes)
		{
			OgreLog("\t " + atribute.first);
			parts.push_back(std::move(extractVertexBuffer(atribute, boundingBox)));
		}

		//Get (if they exists) the blend weights and bone index parts of our vertex array object content
		const auto blendIndicesIt = std::find_if(std::begin(parts), std::end(parts), [](const Ogre_glTF_vertexBufferPart& vertexBufferPart) {
			return (vertexBufferPart.semantic == Ogre::VertexElementSemantic::VES_BLEND_INDICES);
		});

		const auto blendWeightsIt = std::find_if(std::begin(parts), std::end(parts), [](const Ogre_glTF_vertexBufferPart& vertexBufferPart) {
			return (vertexBufferPart.semantic == Ogre::VertexElementSemantic::VES_BLEND_WEIGHTS);
		});

		const auto vertexBuffers = constructVertexBuffer(parts);
		auto vao				 = getVaoManager()->createVertexArrayObject(vertexBuffers, indexBuffer, [&]() -> Ogre::OperationType {
			switch(primitive.mode)
			{
				case TINYGLTF_MODE_LINE:
					OgreLog("Line List");
					return Ogre::OT_LINE_LIST;
				case TINYGLTF_MODE_LINE_LOOP:
					OgreLog("Line Loop");
					return Ogre::OT_LINE_STRIP;
				case TINYGLTF_MODE_POINTS:
					OgreLog("Points");
					return Ogre::OT_POINT_LIST;
				case TINYGLTF_MODE_TRIANGLES:
					OgreLog("Triangle List");
					return Ogre::OT_TRIANGLE_LIST;
				case TINYGLTF_MODE_TRIANGLE_FAN:
					OgreLog("Trinagle Fan");
					return Ogre::OT_TRIANGLE_FAN;
				case TINYGLTF_MODE_TRIANGLE_STRIP:
					OgreLog("Triangle Strip");
					return Ogre::OT_TRIANGLE_STRIP;
				default:
					OgreLog("Unknown");
					throw std::runtime_error("Can't understand primitive mode!");
			};
		}());

		subMesh->mVao[Ogre::VpNormal].push_back(vao);
		subMesh->mVao[Ogre::VpShadow].push_back(vao);

		if(blendIndicesIt != std::end(parts) && blendWeightsIt != std::end(parts))
		{
			//subMesh->_buildBoneAssignmentsFromVertexData();

			//Get the vertexBufferParts from the two iterators
			OgreLog("The vertex buffer contains blend weights and indices information!");
			Ogre_glTF_vertexBufferPart& blendIndices = *blendIndicesIt;
			Ogre_glTF_vertexBufferPart& blendWeights = *blendWeightsIt;

			//Debug sanity check, both should be equals
			OgreLog("Vertex count blendIndex : " + std::to_string(blendIndices.vertexCount));
			OgreLog("Vertex count blendWeight: " + std::to_string(blendWeights.vertexCount));
			OgreLog("Vertex element count blendIndex : " + std::to_string(blendIndices.perVertex));
			OgreLog("Vertex element count blendWeight: " + std::to_string(blendWeights.perVertex));

			//Allocate 2 small arrays to store the bone idexes. (They should be of lenght "4")
			std::vector<Ogre::ushort> vertexBoneIndex(blendIndices.perVertex);
			std::vector<Ogre::Real> vertexBlend(blendWeights.perVertex);

			//Add the attahcments for each bones
			for(size_t vertexIndex = 0; vertexIndex < blendIndices.vertexCount; ++vertexIndex)
			{
				//Fetch the for bone indexes from the buffer
				memcpy(vertexBoneIndex.data(),
					   blendIndices.buffer->dataAddress() + (blendIndices.getPartStride() * vertexIndex),
					   blendIndices.perVertex * sizeof(Ogre::ushort));

				//Fetch the for weights from the buffer
				memcpy(vertexBlend.data(),
					   blendWeights.buffer->dataAddress() + (blendWeights.getPartStride() * vertexIndex),
					   blendWeights.perVertex * sizeof(Ogre::Real));

				//Add the bone assignments to the submesh
				for(size_t i = 0; i < blendIndices.perVertex; ++i)
				{
					auto vba = Ogre::VertexBoneAssignment(vertexIndex, vertexBoneIndex[i], vertexBlend[i]);

					OgreLog("VertexBoneAssignment: " + std::to_string(i) + " over " + std::to_string(blendIndices.perVertex));
					OgreLog(std::to_string(vba.vertexIndex));
					OgreLog(std::to_string(vba.boneIndex));
					OgreLog(std::to_string(vba.weight));

					subMesh->addBoneAssignment(vba);
				}
			}

			//subMesh->_buildBoneIndexMap();
			subMesh->_compileBoneAssignments();
		}
	}

	OgreMesh->_setBounds(boundingBox, true);
	OgreMesh->_setBoundingSphereRadius(boundingBox.getRadius());
	OgreLog("Setting 'bounding sphere radius' from bounds : " + std::to_string(boundingBox.getRadius()));

	return OgreMesh;
}

void Ogre_glTF_modelConverter::debugDump() const
{
	std::stringstream ss;
	ss << "This glTF model has:\n"
	   << model.accessors.size() << " accessors\n"
	   << model.animations.size() << " animations\n"
	   << model.buffers.size() << " buffers\n"
	   << model.bufferViews.size() << " bufferViews\n"
	   << model.materials.size() << " materials\n"
	   << model.meshes.size() << " meshes\n"
	   << model.nodes.size() << " nodes\n"
	   << model.textures.size() << " textures\n"
	   << model.images.size() << " images\n"
	   << model.skins.size() << " skins\n"
	   << model.samplers.size() << " samplers\n"
	   << model.cameras.size() << " cameras\n"
	   << model.scenes.size() << " scenes\n"
	   << model.lights.size() << " lights\n";

	OgreLog(ss.str());
}

bool Ogre_glTF_modelConverter::hasSkins() const
{
	return model.skins.size() > 0;
}

Ogre::VaoManager* Ogre_glTF_modelConverter::getVaoManager()
{
	//Our class shouldn't be able to exist if Ogre hasn't been initalized with a valid render system. This call should allways succeed.
	return Ogre::Root::getSingletonPtr()->getRenderSystem()->getVaoManager();
}

Ogre::IndexBufferPacked* Ogre_glTF_modelConverter::extractIndexBuffer(int accessorID) const
{
	OgreLog("Extracting index buffer");
	const auto& accessor   = model.accessors[accessorID];
	const auto& bufferView = model.bufferViews[accessor.bufferView];
	auto& buffer		   = model.buffers[bufferView.buffer];
	const auto byteStride  = accessor.ByteStride(bufferView);
	const auto indexCount  = accessor.count;
	Ogre::IndexBufferPacked::IndexType type;

	if(byteStride < 0)
		throw std::runtime_error("Can't get valid bytestride from accessor and bufferview. Loading data not possible");

	auto convertTo16Bit{ false };
	switch(accessor.componentType)
	{
		default:
			throw std::runtime_error("Unrecognized index data format");
		case TINYGLTF_COMPONENT_TYPE_BYTE:
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
			convertTo16Bit = true;
		case TINYGLTF_COMPONENT_TYPE_SHORT:
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
		{
			type				= Ogre::IndexBufferPacked::IT_16BIT;
			auto geometryBuffer = Ogre_glTF_geometryBuffer<Ogre::uint16>(indexCount);
			if(convertTo16Bit)
				loadIndexBuffer(geometryBuffer.data(), buffer.data.data(), indexCount, bufferView.byteOffset + accessor.byteOffset, byteStride);
			else
				loadIndexBuffer(geometryBuffer.data(), reinterpret_cast<Ogre::uint16*>(buffer.data.data()), indexCount, bufferView.byteOffset + accessor.byteOffset, byteStride);
			return getVaoManager()->createIndexBuffer(type, indexCount, Ogre::BT_IMMUTABLE, geometryBuffer.dataAddress(), false);
		}
		case TINYGLTF_COMPONENT_TYPE_INT:;
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
		{
			type				= Ogre::IndexBufferPacked::IT_32BIT;
			auto geometryBuffer = Ogre_glTF_geometryBuffer<Ogre::uint32>(indexCount);
			loadIndexBuffer(geometryBuffer.data(), reinterpret_cast<Ogre::uint32*>(buffer.data.data()), indexCount, bufferView.byteOffset + accessor.byteOffset, byteStride);
			return getVaoManager()->createIndexBuffer(type, indexCount, Ogre::BT_IMMUTABLE, geometryBuffer.dataAddress(), false);
		}
	}
}

size_t Ogre_glTF_modelConverter::getVertexBufferElementsPerVertexCount(int type)
{
	switch(type)
	{
		case TINYGLTF_TYPE_VEC2: return 2;
		case TINYGLTF_TYPE_VEC3: return 3;
		case TINYGLTF_TYPE_VEC4: return 4;
		default: return 0;
	}
}

Ogre::VertexElementSemantic Ogre_glTF_modelConverter::getVertexElementScemantic(const std::string& type)
{
	if(type == "POSITION") return Ogre::VES_POSITION;
	if(type == "NORMAL") return Ogre::VES_NORMAL;
	if(type == "TANGENT") return Ogre::VES_TANGENT;
	if(type == "TEXCOORD_0") return Ogre::VES_TEXTURE_COORDINATES;
	if(type == "TEXCOORD_1") return Ogre::VES_TEXTURE_COORDINATES;
	if(type == "COLOR_0") return Ogre::VES_DIFFUSE;
	if(type == "JOINTS_0") return Ogre::VES_BLEND_INDICES;
	if(type == "WEIGHTS_0") return Ogre::VES_BLEND_WEIGHTS;
	return Ogre::VES_COUNT; //Returning this means returning "invalid" here
}

Ogre_glTF_vertexBufferPart Ogre_glTF_modelConverter::extractVertexBuffer(const std::pair<std::string, int>& attribute, Ogre::Aabb& boundingBox) const
{
	const auto elementScemantic = getVertexElementScemantic(attribute.first);

	if(elementScemantic == Ogre::VES_BLEND_INDICES)
	{
		OgreLog("Blend Indices...");
	}

	if(elementScemantic == Ogre::VES_BLEND_WEIGHTS)
	{
		OgreLog("Blend weights");
	}

	const auto& accessor				= model.accessors[attribute.second];
	const auto& bufferView				= model.bufferViews[accessor.bufferView];
	const auto& buffer					= model.buffers[bufferView.buffer];
	const auto vertexBufferByteLen		= bufferView.byteLength;
	const auto numberOfElementPerVertex = getVertexBufferElementsPerVertexCount(accessor.type);
	const auto elementOffsetInBuffer	= bufferView.byteOffset + accessor.byteOffset;
	size_t bufferLenghtInBufferBasicType{ 0 };

	std::unique_ptr<Ogre_glTF_geometryBuffer_base> geometryBuffer{ nullptr };
	Ogre::VertexElementType elementType{};

	switch(accessor.componentType)
	{
		case TINYGLTF_COMPONENT_TYPE_DOUBLE:
			throw std::runtime_error("Double pressision not implemented!");
		case TINYGLTF_COMPONENT_TYPE_FLOAT:
			bufferLenghtInBufferBasicType = (vertexBufferByteLen / sizeof(float));
			geometryBuffer				  = std::make_unique<Ogre_glTF_geometryBuffer<float>>(bufferLenghtInBufferBasicType);
			if(numberOfElementPerVertex == 2) elementType = Ogre::VET_FLOAT2;
			if(numberOfElementPerVertex == 3) elementType = Ogre::VET_FLOAT3;
			if(numberOfElementPerVertex == 4) elementType = Ogre::VET_FLOAT4;
			break;
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
			bufferLenghtInBufferBasicType = (vertexBufferByteLen / sizeof(unsigned short));
			geometryBuffer				  = std::make_unique<Ogre_glTF_geometryBuffer<unsigned short>>(bufferLenghtInBufferBasicType);
			if(numberOfElementPerVertex == 2) elementType = Ogre::VET_USHORT2;
			if(numberOfElementPerVertex == 4) elementType = Ogre::VET_USHORT4;
			break;
		default:
			throw std::runtime_error("Unrecognized vertex buffer coponent type");
	}

	if(bufferView.byteStride == 0) OgreLog("Vertex buffer is 'tightly packed'");
	const auto byteStride = accessor.ByteStride(bufferView);
	if(byteStride < 0) throw std::runtime_error("Can't get valid bytestride from accessor and bufferview. Loading data not possible");
	const auto vertexCount = accessor.count;

	const auto vertexElementLenghtInBytes = numberOfElementPerVertex * geometryBuffer->elementSize();
	OgreLog("A vertex element on this buffer is " + std::to_string(vertexElementLenghtInBytes) + " bytes long");
	for(size_t vertexIndex = 0; vertexIndex < vertexCount; vertexIndex++)
	{
		const auto destOffset   = vertexIndex * vertexElementLenghtInBytes;
		const auto sourceOffset = elementOffsetInBuffer + vertexIndex * byteStride;

		memcpy((geometryBuffer->dataAddress() + destOffset),
			   (buffer.data.data() + sourceOffset),
			   vertexElementLenghtInBytes);
	}

	if(elementScemantic == Ogre::VES_BLEND_INDICES)
	{
		volatile unsigned short* debugbuffer = reinterpret_cast<unsigned short*>(geometryBuffer->dataAddress());
		for(size_t i = 0; i < (vertexElementLenghtInBytes * vertexCount) / sizeof(unsigned short); ++i)
			OgreLog(std::to_string(debugbuffer[i]));
		OgreLog("Done");
	}

	/*
		Update the bounding sizes once, when vertex positions has been read.
	*/
	if(elementScemantic == Ogre::VES_POSITION)
	{
		Ogre::Vector3 minBounds, maxBounds;

		OgreLog("Updating bounding box size: ");

		minBounds.x = accessor.minValues.at(0);
		minBounds.y = accessor.minValues.at(1);
		minBounds.z = accessor.minValues.at(2);
		OgreLog("Setting Min size: " + std::to_string(minBounds.x) + " " + std::to_string(minBounds.y) + " " + std::to_string(minBounds.z));

		maxBounds.x = accessor.maxValues.at(0);
		maxBounds.y = accessor.maxValues.at(1);
		maxBounds.z = accessor.maxValues.at(2);
		OgreLog("Setting Max size: " + std::to_string(maxBounds.x) + " " + std::to_string(maxBounds.y) + " " + std::to_string(maxBounds.z));

		boundingBox.setExtents(minBounds, maxBounds);
	}

	//geometryBuffer->_debugContentToLog();

	return {
		std::move(geometryBuffer),
		elementType,
		elementScemantic,
		vertexCount,
		numberOfElementPerVertex
	};
}
