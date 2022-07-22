#include "renderer.hh"

#include "context.hh"

#ifdef ENABLE_VALIDATION_LAYERS
#    define CHECK_MOVE_PIPELINE(other)                                                 \
        do {                                                                           \
            if (this == std::addressof(other)) die("Cannot move pipeline to itself."); \
        } while (0)
#else
#    define CHECK_MOVE_PIPELINE(other) \
        do {                           \
        } while (0)
#endif

#define MOVE_PIPELINE(other)                                              \
    do {                                                                  \
        CHECK_MOVE_PIPELINE(other);                                       \
        descriptor_pool = other.descriptor_pool;                          \
        descriptor_set_layout = other.descriptor_set_layout;              \
        graphics_pipeline = other.graphics_pipeline;                      \
        pipeline_layout = other.pipeline_layout;                          \
        uniform_buffers = std::move(other.uniform_buffers);               \
        uniform_buffers_memory = std::move(other.uniform_buffers_memory); \
        other.graphics_pipeline = VK_NULL_HANDLE;                         \
    } while (0)

#define DEFAULT_CTORS(renderer)                                                       \
    vk::renderer::renderer(renderer&& other) noexcept : pipeline(std::move(other)) {} \
                                                                                      \
    auto vk::renderer::operator=(renderer&& other) noexcept -> renderer& {            \
        MOVE_PIPELINE(other);                                                         \
        return *this;                                                                 \
    }                                                                                 \
                                                                                      \
    vk::renderer::~renderer() {}

/// ======================================================================
///  Pipeline
/// ======================================================================
vk::pipeline::pipeline(PIPELINE_CTOR_ARGS, const std::vector<VkDescriptorSetLayoutBinding>& descriptor_set_layout_bindings) : ctx(ctx) {
    create_descriptor_set_layout(descriptor_set_layout_bindings);
    create_uniform_buffers();
    create_descriptor_pool(descriptor_set_layout_bindings);
    create_graphics_pipeline(vert_path, frag_path);
}

vk::pipeline::pipeline(pipeline&& other) noexcept : ctx(other.ctx) {
    *this = std::move(other);
}

vk::pipeline& vk::pipeline::operator=(pipeline&& other) noexcept {
    MOVE_PIPELINE(other);
    return *this;
}

vk::pipeline::~pipeline() {
    if (graphics_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx->device, graphics_pipeline, nullptr);
        vkDestroyPipelineLayout(ctx->device, pipeline_layout, nullptr);

        for (u64 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroyBuffer(ctx->device, uniform_buffers[i], nullptr);
            vkFreeMemory(ctx->device, uniform_buffers_memory[i], nullptr);
        }

        vkDestroyDescriptorPool(ctx->device, descriptor_pool, nullptr);
        vkDestroyDescriptorSetLayout(ctx->device, descriptor_set_layout, nullptr);

        graphics_pipeline = VK_NULL_HANDLE;
    }
}

void vk::pipeline::allocate_descriptor_sets(std::vector<VkDescriptorSet>& descriptor_sets) {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptor_set_layout);
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = descriptor_pool;
    alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    alloc_info.pSetLayouts = layouts.data();

    descriptor_sets.resize(MAX_FRAMES_IN_FLIGHT);
    assert_success(vkAllocateDescriptorSets(ctx->device, &alloc_info, descriptor_sets.data()), "failed to allocate descriptor sets");
}

bool vk::pipeline::bound() const {
    return ctx->bound_pipeline == graphics_pipeline;
}

void vk::pipeline::create_descriptor_set_layout(const std::vector<VkDescriptorSetLayoutBinding>& descriptor_set_layout_bindings) {
    VkDescriptorSetLayoutCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = u32(descriptor_set_layout_bindings.size());
    create_info.pBindings = descriptor_set_layout_bindings.data();
    assert_success(vkCreateDescriptorSetLayout(ctx->device, &create_info, nullptr, &descriptor_set_layout), "failed to create descriptor set layout");
}

void vk::pipeline::create_uniform_buffers() {
    auto buffer_size = sizeof(uniform_buffer_object);
    uniform_buffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniform_buffers_memory.resize(MAX_FRAMES_IN_FLIGHT);

    for (u64 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        ctx->create_buffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            uniform_buffers[i], uniform_buffers_memory[i]);
    }
}

void vk::pipeline::create_descriptor_pool(const std::vector<VkDescriptorSetLayoutBinding>& descriptor_set_layout_bindings) {
    std::vector<VkDescriptorPoolSize> pool_sizes{ descriptor_set_layout_bindings.size() };
    for (u64 i = 0; i < descriptor_set_layout_bindings.size(); ++i) {
        pool_sizes[i].type = descriptor_set_layout_bindings[i].descriptorType;
        pool_sizes[i].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    }

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = u32(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    pool_info.maxSets = MAX_FRAMES_IN_FLIGHT;
    assert_success(vkCreateDescriptorPool(ctx->device, &pool_info, nullptr, &descriptor_pool), "failed to create descriptor pool");
}

void vk::pipeline::create_graphics_pipeline(std::string_view vert_path, std::string_view frag_path) {
    /// Create the shader modules.
    auto vert_shader_module = create_shader_module(map_file(vert_path));
    auto frag_shader_module = create_shader_module(map_file(frag_path));
    defer {
        vkDestroyShaderModule(ctx->device, vert_shader_module, nullptr);
        vkDestroyShaderModule(ctx->device, frag_shader_module, nullptr);
    };

    /// Assign the shaders to their corresponding stages.
    VkPipelineShaderStageCreateInfo vert_shader_stage_info{};
    vert_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_shader_stage_info.module = vert_shader_module;
    vert_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo frag_shader_stage_info{};
    frag_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_shader_stage_info.module = frag_shader_module;
    frag_shader_stage_info.pName = "main";

    /// Store them for later.
    VkPipelineShaderStageCreateInfo shader_stages[] = { vert_shader_stage_info, frag_shader_stage_info };

    /// Dynamic state.
    std::vector<VkDynamicState> dynamic_states = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state_info{};
    dynamic_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_info.dynamicStateCount = u32(dynamic_states.size());
    dynamic_state_info.pDynamicStates = dynamic_states.data();

    /// Vertex input.
    auto binding_description = vertex::binding_description();
    auto attribute_descriptions = vertex::attribute_descriptions();

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions = &binding_description;
    vertex_input_info.vertexAttributeDescriptionCount = u32(attribute_descriptions.size());
    vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions.data();

    /// Input assembly.
    VkPipelineInputAssemblyStateCreateInfo input_assembly_info{};
    input_assembly_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_info.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport_state_info{};
    viewport_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state_info.viewportCount = 1;
    viewport_state_info.scissorCount = 1;

    /// Rasteriser.
    VkPipelineRasterizationStateCreateInfo rasteriser_info{};
    rasteriser_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasteriser_info.depthClampEnable = VK_FALSE;
    rasteriser_info.rasterizerDiscardEnable = VK_FALSE;
    rasteriser_info.polygonMode = VK_POLYGON_MODE_FILL;
    rasteriser_info.lineWidth = 1.0f;
    rasteriser_info.cullMode = VK_CULL_MODE_BACK_BIT;
    rasteriser_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasteriser_info.depthBiasEnable = VK_FALSE;

    /// Multisampling.
    VkPipelineMultisampleStateCreateInfo multisampling_info{};
    multisampling_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling_info.sampleShadingEnable = VK_TRUE;
    multisampling_info.rasterizationSamples = ctx->msaa_samples;
    multisampling_info.minSampleShading = .2f;

    /// Colour blending.
    VkPipelineColorBlendAttachmentState colour_blend_attachment{};
    colour_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                             | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colour_blend_attachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colour_blend_info{};
    colour_blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colour_blend_info.logicOpEnable = VK_FALSE;
    colour_blend_info.logicOp = VK_LOGIC_OP_COPY;
    colour_blend_info.attachmentCount = 1;
    colour_blend_info.pAttachments = &colour_blend_attachment;

    /// Depth stencil.
    VkPipelineDepthStencilStateCreateInfo depth_stencil_info{};
    depth_stencil_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_info.depthTestEnable = VK_TRUE;
    depth_stencil_info.depthWriteEnable = VK_TRUE;
    depth_stencil_info.depthCompareOp = VK_COMPARE_OP_LESS; /// Lower depth = closer.
    depth_stencil_info.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_info.stencilTestEnable = VK_FALSE;

    /// Pipeline layout.
    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &descriptor_set_layout;
    assert_success(vkCreatePipelineLayout(ctx->device, &pipeline_layout_info, nullptr, &pipeline_layout), "failed to create pipeline layout");

    /// Finally, create the pipeline.
    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = u32(sizeof shader_stages / sizeof *shader_stages);
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly_info;
    pipeline_info.pViewportState = &viewport_state_info;
    pipeline_info.pRasterizationState = &rasteriser_info;
    pipeline_info.pMultisampleState = &multisampling_info;
    pipeline_info.pDepthStencilState = &depth_stencil_info;
    pipeline_info.pColorBlendState = &colour_blend_info;
    pipeline_info.pDynamicState = &dynamic_state_info;
    pipeline_info.layout = pipeline_layout;
    pipeline_info.renderPass = ctx->render_pass;
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;
    assert_success(vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &graphics_pipeline),
        "failed to create graphics pipeline");
}

auto vk::pipeline::create_shader_module(const std::vector<char>& code) -> VkShaderModule {
    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = code.size();
    create_info.pCode = reinterpret_cast<const u32*>(code.data());

    VkShaderModule shader_module;
    assert_success(vkCreateShaderModule(ctx->device, &create_info, nullptr, &shader_module), "failed to create shader module");
    return shader_module;
}

/// ======================================================================
///  Texture renderer
/// ======================================================================
vk::texture_renderer::texture_renderer(PIPELINE_CTOR_ARGS)
    : pipeline(PIPELINE_CTOR_PARAMS, [] -> std::vector<VkDescriptorSetLayoutBinding> {
          VkDescriptorSetLayoutBinding ubo_layout_binding{};
          ubo_layout_binding.binding = 0;
          ubo_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
          ubo_layout_binding.descriptorCount = 1; /// Dimension.
          ubo_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

          VkDescriptorSetLayoutBinding sampler_layout_binding{};
          sampler_layout_binding.binding = 1;
          sampler_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
          sampler_layout_binding.descriptorCount = 1; /// Dimension.
          sampler_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
          return { ubo_layout_binding, sampler_layout_binding };
      }()) {
    create_texture_sampler();
}

vk::texture_renderer::texture_renderer(texture_renderer&& other) noexcept : pipeline(std::move(other)) {
    texture_sampler = other.texture_sampler;
}

auto vk::texture_renderer::operator=(texture_renderer&& other) noexcept -> texture_renderer& {
    MOVE_PIPELINE(other);

    texture_sampler = other.texture_sampler;
    return *this;
}

vk::texture_renderer::~texture_renderer() {
    if (graphics_pipeline != VK_NULL_HANDLE) vkDestroySampler(ctx->device, texture_sampler, nullptr);
}

void vk::texture_renderer::create_descriptor_sets(std::vector<VkDescriptorSet>& descriptor_sets, VkImageView view) {
    allocate_descriptor_sets(descriptor_sets);

    for (u64 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkDescriptorBufferInfo buffer_info{};
        buffer_info.buffer = uniform_buffers[i];
        buffer_info.offset = 0;
        buffer_info.range = sizeof(uniform_buffer_object);

        VkDescriptorImageInfo image_info{};
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_info.imageView = view;
        image_info.sampler = texture_sampler;

        VkWriteDescriptorSet descriptor_writes[2]{};
        descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_writes[0].dstSet = descriptor_sets[i];
        descriptor_writes[0].dstBinding = 0;
        descriptor_writes[0].dstArrayElement = 0;
        descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptor_writes[0].descriptorCount = 1;
        descriptor_writes[0].pBufferInfo = &buffer_info;

        descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_writes[1].dstSet = descriptor_sets[i];
        descriptor_writes[1].dstBinding = 1;
        descriptor_writes[1].dstArrayElement = 0;
        descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptor_writes[1].descriptorCount = 1;
        descriptor_writes[1].pImageInfo = &image_info;

        vkUpdateDescriptorSets(ctx->device, sizeof descriptor_writes / sizeof *descriptor_writes, descriptor_writes, 0, nullptr);
    }
}

void vk::texture_renderer::draw(VkCommandBuffer command_buffer, const vk::texture_model& m) {
    if (!bound()) {
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
        ctx->bound_pipeline = graphics_pipeline;
    }

    m.verts.bind(command_buffer);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &m.descriptor_sets[ctx->current_frame], 0, nullptr);
    vkCmdDrawIndexed(command_buffer, u32(m.verts.index_count), 1, 0, 0, 0);
}

void vk::texture_renderer::create_texture_sampler() {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(ctx->physical_device, &properties);

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.anisotropyEnable = VK_TRUE;
    sampler_info.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.mipLodBias = 0.0f;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = VK_LOD_CLAMP_NONE;
    assert_success(vkCreateSampler(ctx->device, &sampler_info, nullptr, &texture_sampler), "failed to create texture sampler");
}

/// ======================================================================
///  Geometric renderer
/// ======================================================================
DEFAULT_CTORS(geometric_renderer)

vk::geometric_renderer::geometric_renderer(PIPELINE_CTOR_ARGS)
    : pipeline(PIPELINE_CTOR_PARAMS, [] -> std::vector<VkDescriptorSetLayoutBinding> {
          VkDescriptorSetLayoutBinding ubo_layout_binding{};
          ubo_layout_binding.binding = 0;
          ubo_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
          ubo_layout_binding.descriptorCount = 1; /// Dimension.
          ubo_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
          return { ubo_layout_binding };
      }()) {
    allocate_descriptor_sets(descriptor_sets);

    for (u64 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkDescriptorBufferInfo buffer_info{};
        buffer_info.buffer = uniform_buffers[i];
        buffer_info.offset = 0;
        buffer_info.range = sizeof(uniform_buffer_object);

        VkWriteDescriptorSet descriptor_write{};
        descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_write.dstSet = descriptor_sets[i];
        descriptor_write.dstBinding = 0;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptor_write.descriptorCount = 1;
        descriptor_write.pBufferInfo = &buffer_info;

        vkUpdateDescriptorSets(ctx->device, 1, &descriptor_write, 0, nullptr);
    }
}

auto vk::geometric_renderer::build_geometry() -> geometry_builder {
    return geometry_builder{ this };
}

void vk::geometric_renderer::draw(VkCommandBuffer command_buffer, const vk::geometry& g) {
    if (!bound()) {
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
        ctx->bound_pipeline = graphics_pipeline;
    }

    g.verts.bind(command_buffer);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_sets[ctx->current_frame], 0, nullptr);
    vkCmdDrawIndexed(command_buffer, u32(g.verts.index_count), 1, 0, 0, 0);
}

/// ======================================================================
///  Geometry builder
/// ======================================================================

u32 vk::geometric_renderer::geometry_builder::add(const vertex& v) {
    if (unique_verts.count(v) == 0) {
        u32 idx = u32(verts.size());
        unique_verts[v] = idx;
        verts.push_back(v);
        return idx;
    }

    return unique_verts[v];
}

auto vk::geometric_renderer::geometry_builder::rect(glm::vec2 a, glm::vec2 b, glm::vec3 colour) -> geometry_builder& {
    ///
    /// Determine the corners of the rectangle such that rect_verts[0..3] are in anticlockwise order.
    ///
    vertex rect_verts[4]{};
    rect_verts[0].pos = glm::vec3(a, 0.0f);
    rect_verts[2].pos = glm::vec3(b, 0.0f);

    glm::vec2 v = b - a;

    /// a         /// a
    ///           ///
    ///        b  ///        b
    if ((v.x > 0 && v.y > 0) || (v.x < 0 && v.y < 0)) {
        rect_verts[1].pos = glm::vec3(a.x, a.y + v.y, 0.0f);
        rect_verts[3].pos = glm::vec3(a.x + v.x, a.y, 0.0f);
    }

    ///        b  ///        a
    ///           ///
    /// a         /// b
    else if ((v.x > 0 && v.y < 0) || (v.x < 0 && v.y > 0)) {
        rect_verts[1].pos = glm::vec3(a.x + v.x, a.y, 0.0f);
        rect_verts[3].pos = glm::vec3(a.x, a.y + v.y, 0.0f);
    }

    /// a       b
    else {
        rect_verts[1].pos = rect_verts[2].pos;
        rect_verts[3].pos = rect_verts[0].pos;
    }

    ///
    /// Generate the rectangle.
    ///
    u32 rect_indices[4];

    for (u64 i = 0; i < sizeof rect_verts / sizeof *rect_verts; ++i) {
        rect_verts[i].colour = colour;
        rect_indices[i] = add(rect_verts[i]);
    }

    indices.push_back(rect_indices[0]);
    indices.push_back(rect_indices[1]);
    indices.push_back(rect_indices[2]);
    indices.push_back(rect_indices[2]);
    indices.push_back(rect_indices[3]);
    indices.push_back(rect_indices[0]);

    return *this;
}

vk::geometric_renderer::geometry_builder::operator geometry() const {
    return { .verts = vertex_buffer(r->ctx, verts, indices) };
}